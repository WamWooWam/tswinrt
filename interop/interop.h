#pragma once
#include <vector>
#include <inspectable.h>
#include <roapi.h>
#include <winrt/base.h>
#include <winmd_reader.h>
#include "../helpers.h"

using namespace winmd::reader;

namespace interop {


    template <typename T>
    uint8_t* begin_bytes(T& x) {
        return reinterpret_cast<uint8_t*>(&x);
    }

    template <typename T>
    uint8_t* end_bytes(T& x) {
        return reinterpret_cast<uint8_t*>(&x + 1);
    }

    template <typename T>
    const uint8_t* begin_bytes(T const& x) {
        return reinterpret_cast<const uint8_t*>(&x);
    }

    template <typename T>
    const uint8_t* end_bytes(T const& x) {
        return reinterpret_cast<const uint8_t*>(&x + 1);
    }


    void const* compute_function_pointer(void const* const instance, unsigned const slot) {
        // There are two levels of indirection to get to the function pointer:
        //
        //                  object            vtable
        //               +----------+      +----------+
        // instance ---> | vptr     | ---> | slot 0   |
        //               |~~~~~~~~~~|      | slot 1   |
        //                                 | slot 2   |
        //                                 |~~~~~~~~~~|
        //
        // This is fundamentally unsafe, so be very careful when calling. :-)
        return (*reinterpret_cast<void const* const* const*>(instance))[slot];
    }

    uint32_t compute_method_slot_index(MethodDef& method) {
        // TODO We should really add this as a member function on 'method'.  We can trivially compute
        // it by determining the index of the method in its element context table.  We should also
        // only scan declared methods, not all methods.
        TypeDef& parent_type = method.Parent();
        uint32_t slot_index(0);
        for (auto& it : parent_type.MethodList()) {
            if (it == method)
                break;

            ++slot_index;
        }

        // TODO Error checking?
        return slot_index;
    }

    IInspectable* query_interface(IInspectable* const instance, TypeDef& interface_type) {
        winrt::guid interface_guid(get_guid(interface_type));

        IInspectable* interface_pointer;
        auto hr = instance->QueryInterface(interface_guid, reinterpret_cast<void**>(&interface_pointer));
        if (FAILED(hr))
            return nullptr;

        return interface_pointer;
    }

    IInspectable* convert_to_interface(const void* argument, winrt::guid const& interface_guid) {
        winrt::com_ptr<IInspectable> inspectable_interface;
        IInspectable* inspectable_object(reinterpret_cast<IInspectable*>((void*)argument));
        winrt::throw_hresult(inspectable_object->QueryInterface(interface_guid, inspectable_interface.put_void()));
        return inspectable_interface.get();
    }


    namespace x64 {
        extern "C" int tswinrt_windows_runtime_x64_fastcall_thunk(void const* fp, void const* args, void const* types, uint64_t count);

        /// Flags for the 'types' array passed to the fastcall thunk
        /// The enumerator values must match those described in the documentation of the thunk procedure
        enum class argument_type : std::uint64_t {
            integer = 0,
            double_precision_real = 1,
            single_precision_real = 2
        };

        /// Frame builder that constructs an argument frame in the form required by the fastcall thunk
        class argument_frame {
        public:
            const void* arguments() const { return _arguments.data(); }
            const void* types() const { return _types.data(); }
            const std::uint64_t count() const { return _arguments.size(); }

            void push(float const x) {
                _arguments.insert(_arguments.end(), begin_bytes(x), end_bytes(x));
                _arguments.resize(_arguments.size() + (8 - sizeof(float)));
                _types.push_back(argument_type::single_precision_real);
            }

            void push(double const x) {
                _arguments.insert(_arguments.end(), begin_bytes(x), end_bytes(x));
                _types.push_back(argument_type::double_precision_real);
            }

            template <typename T>
            auto push(T const& x) -> typename std::enable_if<sizeof(T) <= 8>::type {
                _arguments.insert(_arguments.end(), begin_bytes(x), end_bytes(x));
                _arguments.resize(_arguments.size() + (8 - sizeof(T)));
                _types.push_back(argument_type::integer);
            }

        private:
            std::vector<uint8_t> _arguments;
            std::vector<argument_type> _types;
        };

        /// Call invoker for x64 fastcall functions
        class call_invoker {
        public:
            static winrt::hresult invoke(
                method_signature const& method,
                IInspectable* instance,
                void* result,
                std::vector<std::pair<type_semantics, void*>> const& arguments);

        private:
            static void convert_and_insert(
                type_semantics const& parameter_type,
                void* argument,
                argument_frame& frame);
        };
    }

    namespace x86 {
        class argument_frame {
        public:
            const uint8_t* begin() const {
                return _data.data();
            }

            const uint8_t* end() {
                return _data.data() + _data.size();
            }

            const uint8_t* data() {
                return _data.data();
            }

            uint32_t size() {
                return _data.size();
            }

            void align_to(uint32_t alignment) {
                if (_data.empty())
                    return;

                const uint32_t bytes_to_insert(alignment - (_data.size() % alignment));
                _data.resize(_data.size() + bytes_to_insert);
            }

            void push(const uint8_t* first, const uint8_t* last) {
                _data.insert(_data.end(), first, last);
            }

        private:
            std::vector<uint8_t> _data;
        };

        /// Call invoker for x86 stdcall functions
        class call_invoker {
        public:
            winrt::hresult invoke(MethodDef const& method, IInspectable** instancePtr, void* result, std::vector<void*> const& arguments) {
                // We can only call a method defined by an interface implemented by the runtime type, so
                // we re-resolve the method against the interfaces of its declaring type.  If it has
                // already been resolved to an interface method, this is a no-op transformation.
                MethodDef interface_method;
                TypeDef& parent_type = method.Parent();

                bool is_static = false;
                std::string type_name = std::string(parent_type.TypeNamespace()) + "." + std::string(parent_type.TypeName());
                std::string method_name = std::string(method.Name());
                if (get_category(parent_type) != category::interface_type) {
                    interface_method = get_interface_method(parent_type, (MethodDef&)method, is_static);
                }

                if (!interface_method)
                    return E_FAIL;

                IInspectable* instance = *instancePtr;
                if (instance == nullptr) {
                    instance = get_or_create_instance(method.Parent(), interface_method.Parent(), is_static);
                }

                if (instance == nullptr) {
                    return E_FAIL;
                }

                // Next, we need to compute the vtable slot of the method and QI to get the correct
                // interface pointer in order to obtain the function pointer.
                uint32_t const method_slot(compute_method_slot_index(interface_method));
                auto const interface_pointer(query_interface(instance, interface_method.Parent()));

                // We compute the function pointer from the vtable.  '6' is the well-known offset of all
                // Windows Runtime interface methods (IUnknown has three functions, and IInspectable has
                // an additional three functions).
                void const* const fp(compute_function_pointer(interface_pointer, method_slot + 6));

                // We construct the argument frame, by converting each argument to the correct type and
                // appending it to an array.  In stdcall, arguments are pushed onto the stack left-to-right.
                // Because the stack is upside-down (i.e., it grows top-to-bottom), we push the arguments
                // into our argument frame right-to-left.
                argument_frame frame;

                // Every function is called via an interface pointer.  That is always the first argument:
                void const* const raw_interface_pointer(interface_pointer);
                frame.push(begin_bytes(raw_interface_pointer), end_bytes(raw_interface_pointer));


                method_signature method_sig{ method };
                // Next, we iterate over the arguments and parameters, convert each argument to the correct
                // parameter type, and push the argument into the frame:
                auto count = method_sig.params().size();
                auto p_it = method_sig.params().begin();
                auto a_it(begin(arguments));
                for (; p_it != method_sig.params().end() && a_it != end(arguments); ++p_it, ++a_it) {
                    convert_and_insert(get_type_semantics((*p_it).second->Type()), (*a_it), frame);
                }

                if (p_it != method_sig.params().end() || a_it != end(arguments)) {
                    throw std::exception("method arity does not match argument count");
                }

                if (method.Signature().ReturnType()) {
                    frame.push(begin_bytes(result), end_bytes(result));
                }
                //else if (result != nullptr) {
                //    throw std::exception("attempted to call a void-returning function with a result pointer");
                //}

                // Due to promotion and padding, all argument frames should have a size divisible by 4.
                // In order to avoid writing inline assembly to move the arguments frame onto the stack
                // and issue the call instruction, we have a set of function template instantiations
                // that handle invocation for us.
                switch (frame.size()) {
                case 4:
                    return invoke_with_frame<4>(fp, frame.data());
                case 8:
                    return invoke_with_frame<8>(fp, frame.data());
                case 12:
                    return invoke_with_frame<12>(fp, frame.data());
                case 16:
                    return invoke_with_frame<16>(fp, frame.data());
                case 20:
                    return invoke_with_frame<20>(fp, frame.data());
                case 24:
                    return invoke_with_frame<24>(fp, frame.data());
                case 28:
                    return invoke_with_frame<28>(fp, frame.data());
                case 32:
                    return invoke_with_frame<32>(fp, frame.data());
                case 36:
                    return invoke_with_frame<36>(fp, frame.data());
                case 40:
                    return invoke_with_frame<40>(fp, frame.data());
                case 44:
                    return invoke_with_frame<44>(fp, frame.data());
                case 48:
                    return invoke_with_frame<48>(fp, frame.data());
                case 52:
                    return invoke_with_frame<52>(fp, frame.data());
                case 56:
                    return invoke_with_frame<56>(fp, frame.data());
                case 60:
                    return invoke_with_frame<60>(fp, frame.data());
                case 64:
                    return invoke_with_frame<64>(fp, frame.data());
                }

                // If we hit this, we just need to add additional cases above.
                throw std::exception("size of requested frame is out of range");
            }


        private:
            void convert_and_insert(
                type_semantics const& parameter_type,
                void* argument,
                argument_frame& frame) {
                call(
                    parameter_type,
                    [&](fundamental_type const& type) {
                        switch (type) {
                        case fundamental_type::Int8:
                        case fundamental_type::Int16:
                        case fundamental_type::Int32:
                        case fundamental_type::UInt8:
                        case fundamental_type::UInt16:
                        case fundamental_type::UInt32:
                        case fundamental_type::Boolean: {
                            auto value(*reinterpret_cast<uint32_t*>(argument));
                            frame.push(begin_bytes(value), end_bytes(value));
                            break;
                        }

                        case fundamental_type::Int64:
                        case fundamental_type::UInt64: {
                            auto value(*reinterpret_cast<uint64_t*>(argument));
                            frame.push(begin_bytes(value), end_bytes(value));
                            break;
                        }

                        case fundamental_type::Float: {
                            auto value(*reinterpret_cast<float*>(argument));
                            frame.push(begin_bytes(value), end_bytes(value));
                            break;
                        }

                        case fundamental_type::Double: {
                            auto value(*reinterpret_cast<double*>(argument));
                            frame.push(begin_bytes(value), end_bytes(value));
                            break;
                        }

                        case fundamental_type::Char:
                        default:
                            throw_invalid("Unsupported fundamental_type");
                        }
                    },
                    [&](type_definition const& type) {
                        switch (get_category(type)) {
                        case category::enum_type:
                            convert_and_insert(type.get_enum_definition().m_typedef, argument, frame);
                            break;
                        case category::interface_type:
                        case category::class_type: {
                            const auto value = convert_to_interface(argument, get_guid(type));
                            frame.push(begin_bytes(value), end_bytes(value));
                            break;
                        }
                        case category::struct_type:
                        default:
                            throw_invalid("Unsupported type_definition");
                            break;
                        }
                    },
                    [](auto) {
                        throw_invalid("type definition expected");
                    });
            }

            template <uint32_t FrameSize>
            static winrt::hresult invoke_with_frame(void const* fp, const uint8_t* frame) {
                struct frame_type {
                    uint8_t _value[FrameSize];
                };
                typedef HRESULT(__stdcall * method_signature)(frame_type);

                frame_type const* const typed_frame(reinterpret_cast<frame_type const*>(frame));
                method_signature const typed_fp(reinterpret_cast<method_signature>(fp));

                __try {
                    // Here we go!
                    return typed_fp(*typed_frame);
                }
                __except() {
                    return E_FAIL;
                }
            }

            IInspectable* get_or_create_instance(TypeDef& primary_type, TypeDef& interface_type, bool is_static) {
                auto get_system_type = [&](auto&& signature) -> TypeDef {
                    for (auto&& arg : signature.FixedArgs()) {
                        if (auto type_param = std::get_if<ElemSig::SystemType>(&std::get<ElemSig>(arg.value).value)) {
                            return primary_type.get_cache().find_required(type_param->name);
                        }
                    }

                    return {};
                };

                std::string type_name = std::string(primary_type.TypeNamespace()) + "." + std::string(primary_type.TypeName());
                std::string iface_name = std::string(interface_type.TypeNamespace()) + "." + std::string(interface_type.TypeName());
                IInspectable* object;

                if (is_static) {
                    auto type_iid = get_guid(interface_type);
                    auto hr = RoGetActivationFactory(reinterpret_cast<HSTRING>(winrt::get_abi(winrt::to_hstring(type_name))), type_iid, (void**)&object);

                    if (FAILED(hr))
                        return nullptr;
                }
                else {
                    if (!has_attribute(primary_type, "Windows.Foundation.Metadata", "ActivatableAttribute"))
                        return nullptr;

                    auto hr = RoActivateInstance(reinterpret_cast<HSTRING>(winrt::get_abi(winrt::to_hstring(type_name))), &object);
                    if (FAILED(hr))
                        return nullptr;

                    //winrt::com_ptr<IInspectable> factory;
                    //auto attribute = get_attribute(primary_type, "Windows.Foundation.Metadata", "ActivatableAttribute");
                    //auto factory_type = get_system_type(attribute.Value());

                    //if (!factory_type)
                    //    factory_type = primary_type;

                    //auto factory_iid = get_guid(factory_type);
                    //MethodDef method;

                    //for (auto&& method_def : factory_type.MethodList()) {
                    //    std::cout << method_def.Name() << " ";
                    //    if (!method_def.Signature().ReturnType()) {
                    //        continue;
                    //    }

                    //    auto semantics = std::get_if<TypeDef>(&get_type_semantics(method_def.Signature().ReturnType().Type()));
                    //    if (semantics == nullptr || *semantics != primary_type) {
                    //        continue;
                    //    }

                    //    if (distance(method_def.Signature().Params()) != 0)
                    //        continue;

                    //    method = method_def;
                    //}

                    //if (!method)
                    //    return nullptr;

                    //auto hr = RoGetActivationFactory(reinterpret_cast<HSTRING>(winrt::get_abi(winrt::to_hstring(type_name))), factory_iid, object.put_void());
                    //if (FAILED(hr))
                    //    return nullptr;

                    //IInspectable* raw_factory = factory.get();
                    //hr = this->invoke(method, &raw_factory, object.put_void(), std::vector<void*>());

                    //if (FAILED(hr))
                    //    return nullptr;
                }

                return object;
            }
        };
    }

#if WIN32
    using namespace x86;
#endif

    IInspectable* create_inspectable_instance(const TypeDef& type, std::vector<void*> const& arguments);
};