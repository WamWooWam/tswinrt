#pragma once
#include <stack>
#include <algorithm>
#include <cctype>
#include <string>
#include <winmd_reader.h>

using namespace std::literals;
using namespace winmd::reader;

[[noreturn]] inline void throw_invalid(std::string const &message) {
    throw std::invalid_argument(message);
}

template <typename... T>
[[noreturn]] inline void throw_invalid(std::string message, T const &...args) {
    (message.append(args), ...);
    throw std::invalid_argument(message);
}

template <typename... T>
struct visit_overload : T... {
    using T::operator()...;
};

template <typename V, typename... C>
auto call(V &&variant, C &&...call) {
    return std::visit(visit_overload<C...>{ std::forward<C>(call)... }, std::forward<V>(variant));
}

std::string get_mapped_element_type(ElementType elementType);

void are_equal(const winmd::reader::MethodDef &iface_method, winmd::reader::MethodDef &method, int &retflag);

static inline bool starts_with(std::string_view const &value, std::string_view const &match) noexcept {
    return 0 == value.compare(0, match.size(), match);
}

static bool is_remove_overload(MethodDef const &method) {
    return method.SpecialName() && starts_with(method.Name(), "remove_");
}

template <typename T>
bool has_attribute(T const &row, std::string_view const &type_namespace, std::string_view const &type_name) {
    return static_cast<bool>(get_attribute(row, type_namespace, type_name));
}

static bool is_noexcept(MethodDef const &method) {
    return is_remove_overload(method) || has_attribute(method, "Windows.Foundation.Metadata", "NoExceptionAttribute");
}

static bool is_noexcept(Property const &prop) {
    return has_attribute(prop, "Windows.Foundation.Metadata", "NoExceptionAttribute");
}

bool is_exclusive_to(TypeDef const &type) {
    return get_category(type) == category::interface_type && has_attribute(type, "Windows.Foundation.Metadata"sv, "ExclusiveToAttribute"sv);
}

bool is_flags_enum(TypeDef const &type) {
    return get_category(type) == category::enum_type && has_attribute(type, "System"sv, "FlagsAttribute"sv);
}

bool is_api_contract_type(TypeDef const &type) {
    return get_category(type) == category::struct_type && has_attribute(type, "Windows.Foundation.Metadata"sv, "ApiContractAttribute"sv);
}

bool is_attribute_type(TypeDef const &type) {
    return get_category(type) == category::class_type && extends_type(type, "System"sv, "Attribute"sv);
}

bool is_ptype(TypeDef const &type) {
    return distance(type.GenericParam()) > 0;
}

bool is_static(TypeDef const &type) {
    return get_category(type) == category::class_type && type.Flags().Abstract();
}

bool is_constructor(MethodDef const &method) {
    return method.Flags().RTSpecialName() && method.Name() == ".ctor";
}

bool is_special(MethodDef const &method) {
    return method.SpecialName() || method.Flags().RTSpecialName();
}

bool is_static(MethodDef const &method) {
    return method.Flags().Static();
}

auto get_delegate_invoke(TypeDef const &type) {
    XLANG_ASSERT(get_category(type) == category::delegate_type);

    for (auto &&method : type.MethodList()) {
        if (method.SpecialName() && (method.Name() == "Invoke")) {
            return method;
        }
    }

    throw_invalid("Invoke method not found");
}

auto get_guid(TypeDef const &type) {
    auto attribute = get_attribute(type, "Windows.Foundation.Metadata", "GuidAttribute");
    if (!attribute) {
        throw_invalid("'Windows.Foundation.Metadata.GuidAttribute' attribute for type '", type.TypeNamespace(), ".", type.TypeName(), "' not found");
    }

    using std::get;
    auto args = attribute.Value().FixedArgs();
    auto get_arg = [&](decltype(args)::size_type index) { return get<ElemSig>(args[index].value).value; };

    winrt::guid guid{};
    guid.Data1 = get<uint32_t>(get_arg(0));
    guid.Data2 = get<uint16_t>(get_arg(1));
    guid.Data3 = get<uint16_t>(get_arg(2));
    guid.Data4[0] = get<uint8_t>(get_arg(3));
    guid.Data4[1] = get<uint8_t>(get_arg(4));
    guid.Data4[2] = get<uint8_t>(get_arg(5));
    guid.Data4[3] = get<uint8_t>(get_arg(6));
    guid.Data4[4] = get<uint8_t>(get_arg(7));
    guid.Data4[5] = get<uint8_t>(get_arg(8));
    guid.Data4[6] = get<uint8_t>(get_arg(9));
    guid.Data4[7] = get<uint8_t>(get_arg(10));

    return guid;
}


/// Tests whether the ranges `[first0, last0)` and `[first1, last1)` are equal using `==`
///
/// If the ranges are not of equal length, `false` is returned.
template <typename InputIterator0, typename InputIterator1>
auto range_checked_equal(InputIterator0 first0,
    InputIterator0 const last0,
    InputIterator1 first1,
    InputIterator1 const last1) -> bool {
    while (first0 != last0 && first1 != last1 && *first0 == *first1) {
        ++first0;
        ++first1;
    }

    return first0 == last0 && first1 == last1;
}

enum class fundamental_type {
    Boolean,
    Char,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float,
    Double,
    String,
};

struct generic_type_instance;
struct object_type {
};
struct guid_type {
};
struct type_type {
};
using type_definition = TypeDef;
using generic_type_index = GenericTypeIndex;
using generic_type_param = GenericParam;

using type_semantics = std::variant<
    fundamental_type,
    object_type,
    guid_type,
    type_type,
    type_definition,
    generic_type_instance,
    generic_type_index,
    generic_type_param>;

struct generic_type_instance {
    type_definition generic_type;
    std::vector<type_semantics> generic_args{};
};

struct generic_args {
    std::vector<std::vector<type_semantics>> _stack;
    size_t _scope = 0;

    struct args_guard {
        explicit args_guard(generic_args *owner = nullptr)
            : _owner(owner) {
        }

        ~args_guard() {
            if (_owner) {
                _owner->pop();
            }
        }

        args_guard(args_guard &&other) = delete;
        args_guard &operator=(args_guard const &) = delete;
        args_guard &operator=(args_guard &&other) = delete;
        generic_args *_owner;
    };

    struct scope_guard {
        size_t _scope;

        explicit scope_guard(generic_args &owner, size_t scope)
            : _owner(&owner), _scope(scope) {
            _scope = std::exchange(_owner->_scope, _scope);
        }

        ~scope_guard() {
            if (_owner) {
                _scope = std::exchange(_owner->_scope, _scope);
            }
        }

        scope_guard(scope_guard &&other)
            : _owner(other._owner), _scope(other._scope) {
            other._owner = nullptr;
        }

        scope_guard &operator=(scope_guard const &) = delete;
        scope_guard &operator=(scope_guard &&other) = delete;
        generic_args *_owner;
    };

    [[nodiscard]] auto push(std::pair<GenericParam, GenericParam> const &range) {
        if (empty(range)) {
            return args_guard{ nullptr };
        }

        _stack.emplace_back(begin(range), end(range));
        return args_guard{ this };
    }

    [[nodiscard]] auto push(generic_type_instance const &type) {
        XLANG_ASSERT(!type.generic_args.empty());
        _stack.push_back(type.generic_args);
        return args_guard{ this };
    }

    auto get(uint32_t index) {
        size_t scope = _scope > 0 ? _scope - 1 : _stack.size();
        for (size_t i = scope; i > 0; --i) {
            auto &&args = &_stack[i - 1];
            if (index >= args->size()) {
                throw_invalid("Generic index out of range");
            }

            auto &semantics = (*args)[index];
            if (auto gti = std::get_if<generic_type_index>(&semantics)) {
                index = gti->index;
                continue;
            }
            return std::pair{ semantics, scope_guard(*this, i) };
        }
        throw_invalid("No generic arguments");
    }

    void pop() {
        _stack.pop_back();
    }
};

type_semantics get_type_semantics(TypeSig const &signature);

type_semantics get_type_semantics(GenericTypeInstSig const &type) {
    auto generic_type_helper = [&type]() {
        switch (type.GenericType().type()) {
        case TypeDefOrRef::TypeDef:
            return type.GenericType().TypeDef();
        case TypeDefOrRef::TypeRef:
            return find_required(type.GenericType().TypeRef());
        }

        throw_invalid("invalid TypeDefOrRef value for GenericTypeInstSig.GenericType");
    };

    auto gti = generic_type_instance{ generic_type_helper() };

    for (auto &&arg : type.GenericArgs()) {
        gti.generic_args.push_back(get_type_semantics(arg));
    }

    return gti;
}

type_semantics get_type_semantics(coded_index<TypeDefOrRef> const &type) {
    switch (type.type()) {
    case TypeDefOrRef::TypeDef:
        return type.TypeDef();
    case TypeDefOrRef::TypeRef: {
        auto type_ref = type.TypeRef();
        if (type_ref.TypeName() == "Attribute" && type_ref.TypeNamespace() == "System") {
            return object_type{};
        }

        if (type_ref.TypeName() == "Guid" && type_ref.TypeNamespace() == "System") {
            return guid_type{};
        }

        if (type_ref.TypeName() == "Object" && type_ref.TypeNamespace() == "System") {
            return object_type{};
        }

        if (type_ref.TypeName() == "Type" && type_ref.TypeNamespace() == "System") {
            return type_type{};
        }

        return find_required(type_ref);
    }
    case TypeDefOrRef::TypeSpec:
        return get_type_semantics(type.TypeSpec().Signature().GenericTypeInst());
    }

    throw_invalid("TypeDefOrRef not supported");
}

namespace impl {
    template <class... Ts>
    struct overloaded : Ts... {
        using Ts::operator()...;
    };
    template <class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;
} // namespace impl

type_semantics get_type_semantics(TypeSig const &signature) {
    return std::visit(
        impl::overloaded{
            [](ElementType type) -> type_semantics {
                switch (type) {
                case ElementType::Boolean:
                    return fundamental_type::Boolean;
                case ElementType::Char:
                    return fundamental_type::Char;
                case ElementType::I1:
                    return fundamental_type::Int8;
                case ElementType::U1:
                    return fundamental_type::UInt8;
                case ElementType::I2:
                    return fundamental_type::Int16;
                case ElementType::U2:
                    return fundamental_type::UInt16;
                case ElementType::I4:
                    return fundamental_type::Int32;
                case ElementType::U4:
                    return fundamental_type::UInt32;
                case ElementType::I8:
                    return fundamental_type::Int64;
                case ElementType::U8:
                    return fundamental_type::UInt64;
                case ElementType::R4:
                    return fundamental_type::Float;
                case ElementType::R8:
                    return fundamental_type::Double;
                case ElementType::String:
                    return fundamental_type::String;
                case ElementType::Object:
                    return object_type{};
                }
                throw_invalid("element type not supported: " + get_mapped_element_type(type));
            },
            [](coded_index<TypeDefOrRef> type) -> type_semantics {
                return get_type_semantics(type);
            },
            [](GenericTypeIndex var) -> type_semantics { return generic_type_index{ var.index }; },
            [](GenericTypeInstSig sig) -> type_semantics { return get_type_semantics(sig); },
            [](GenericMethodTypeIndex) -> type_semantics { throw_invalid("Generic methods not supported"); } },
        signature.Type());
}

struct method_signature {
    using param_t = std::pair<Param, ParamSig const *>;

    explicit method_signature(MethodDef const &method) : m_method(method.Signature()) {
        auto params = method.ParamList();

        if (m_method.ReturnType() && params.first != params.second && params.first.Sequence() == 0) {
            m_return = params.first;
            ++params.first;
        }

        for (uint32_t i{}; i != size(m_method.Params()); ++i) {
            m_params.emplace_back(params.first + i, &m_method.Params().first[i]);
        }
    }

    std::vector<param_t> &params() {
        return m_params;
    }

    std::vector<param_t> const &params() const {
        return m_params;
    }

    auto const &return_signature() const {
        return m_method.ReturnType();
    }

    auto return_param_name(std::string_view default_name = "__return_value__") const {
        if (m_return) {
            return m_return.Name();
        }
        else {
            return default_name;
        }
    }

    bool has_params() const {
        return !m_params.empty();
    }

private:
    MethodDefSig m_method;
    std::vector<param_t> m_params;
    Param m_return;
};

enum class param_category {
    in,
    ref,
    out,
    pass_array,
    fill_array,
    receive_array,
};

auto get_param_category(method_signature::param_t const &param) {
    if (param.second->Type().is_szarray()) {
        if (param.first.Flags().In()) {
            return param_category::pass_array;
        }
        else if (param.second->ByRef()) {
            XLANG_ASSERT(param.first.Flags().Out());
            return param_category::receive_array;
        }
        else {
            XLANG_ASSERT(param.first.Flags().Out());
            return param_category::fill_array;
        }
    }
    else {
        if (param.first.Flags().Out()) {
            return param_category::out;
        }
        else if (param.second->ByRef()) {
            return param_category::ref;
        }
        else {
            return param_category::in;
        }
    }
}

auto get_property_methods(Property const &prop) {
    MethodDef get_method{}, set_method{};

    for (auto &&method_semantic : prop.MethodSemantic()) {
        auto semantic = method_semantic.Semantic();

        if (semantic.Getter()) {
            get_method = method_semantic.Method();
        }
        else if (semantic.Setter()) {
            set_method = method_semantic.Method();
        }
        else {
            throw_invalid("Properties can only have get and set methods");
        }
    }

    XLANG_ASSERT(get_method || set_method);

    if (get_method && set_method) {
        XLANG_ASSERT(get_method.Flags().Static() == set_method.Flags().Static());
    }

    return std::make_tuple(get_method, set_method);
}

auto get_event_methods(Event const &evt) {
    MethodDef add_method{}, remove_method{};

    for (auto &&method_semantic : evt.MethodSemantic()) {
        auto semantic = method_semantic.Semantic();

        if (semantic.AddOn()) {
            add_method = method_semantic.Method();
        }
        else if (semantic.RemoveOn()) {
            remove_method = method_semantic.Method();
        }
        else {
            throw_invalid("Events can only have add and remove methods");
        }
    }

    XLANG_ASSERT(add_method);
    XLANG_ASSERT(remove_method);
    XLANG_ASSERT(add_method.Flags().Static() == remove_method.Flags().Static());

    return std::make_tuple(add_method, remove_method);
}

inline coded_index<TypeDefOrRef> get_default_interface(TypeDef const &type) {
    auto impls = type.InterfaceImpl();

    for (auto &&impl : impls) {
        if (has_attribute(impl, "Windows.Foundation.Metadata", "DefaultAttribute")) {
            return impl.Interface();
        }
    }

    if (!empty(impls)) {
        throw_invalid("Type '", type.TypeNamespace(), ".", type.TypeName(), "' does not have a default interface");
    }

    return {};
}

struct mapped_type {
    std::string_view abi_name;
    std::string_view mapped_namespace;
    std::string_view mapped_name;
    bool requires_marshaling;
    bool has_custom_members_output;
};

inline const std::initializer_list<mapped_type> get_mapped_types_in_namespace(std::string_view typeNamespace) {
    static const struct
    {
        std::string_view name_space;
        std::initializer_list<mapped_type> types;
    } mapped_types[] = {
        // Make sure to keep this table consistent with the registrations in WinRT.Runtime/Projections.cs
        // NOTE: Must keep namespaces sorted (outer) and abi type names sorted (inner)
        { "Microsoft.UI.Xaml",
            {
                { "CornerRadius", "Microsoft.UI.Xaml", "CornerRadius" },
                { "CornerRadiusHelper" },
                { "Duration", "Microsoft.UI.Xaml", "Duration" },
                { "DurationHelper" },
                { "DurationType", "Microsoft.UI.Xaml", "DurationType" },
                { "GridLength", "Microsoft.UI.Xaml", "GridLength" },
                { "GridLengthHelper" },
                { "GridUnitType", "Microsoft.UI.Xaml", "GridUnitType" },
                { "ICornerRadiusHelper" },
                { "ICornerRadiusHelperStatics" },
                { "IDurationHelper" },
                { "IDurationHelperStatics" },
                { "IGridLengthHelper" },
                { "IGridLengthHelperStatics" },
                { "IThicknessHelper" },
                { "IThicknessHelperStatics" },
                { "Thickness", "Microsoft.UI.Xaml", "Thickness" },
                { "ThicknessHelper" },
                { "IXamlServiceProvider", "System", "IServiceProvider" },
            } },
        { "Microsoft.UI.Xaml.Controls.Primitives",
            {
                { "GeneratorPosition", "Microsoft.UI.Xaml.Controls.Primitives", "GeneratorPosition" },
                { "GeneratorPositionHelper" },
                { "IGeneratorPositionHelper" },
                { "IGeneratorPositionHelperStatics" },
            } },
        { "Microsoft.UI.Xaml.Data",
            {
                { "DataErrorsChangedEventArgs", "System.ComponentModel", "DataErrorsChangedEventArgs" },
                { "INotifyDataErrorInfo", "System.ComponentModel", "INotifyDataErrorInfo", true, true },
                { "INotifyPropertyChanged", "System.ComponentModel", "INotifyPropertyChanged" },
                { "PropertyChangedEventArgs", "System.ComponentModel", "PropertyChangedEventArgs" },
                { "PropertyChangedEventHandler", "System.ComponentModel", "PropertyChangedEventHandler" },
            } },
        { "Microsoft.UI.Xaml.Input",
            { { "ICommand", "System.Windows.Input", "ICommand", true } } },
        { "Microsoft.UI.Xaml.Interop",
            {
                { "IBindableIterable", "System.Collections", "IEnumerable", true },
                { "IBindableVector", "System.Collections", "IList", true },
                { "INotifyCollectionChanged", "System.Collections.Specialized", "INotifyCollectionChanged", true },
                { "NotifyCollectionChangedAction", "System.Collections.Specialized", "NotifyCollectionChangedAction" },
                { "NotifyCollectionChangedEventArgs", "System.Collections.Specialized", "NotifyCollectionChangedEventArgs", true },
                { "NotifyCollectionChangedEventHandler", "System.Collections.Specialized", "NotifyCollectionChangedEventHandler", true },
            } },
        { "Microsoft.UI.Xaml.Media",
            {
                { "IMatrixHelper" },
                { "IMatrixHelperStatics" },
                { "Matrix", "Microsoft.UI.Xaml.Media", "Matrix" },
                { "MatrixHelper" },
            } },
        { "Microsoft.UI.Xaml.Media.Animation",
            { { "IKeyTimeHelper" },
                { "IKeyTimeHelperStatics" },
                { "IRepeatBehaviorHelper" },
                { "IRepeatBehaviorHelperStatics" },
                { "KeyTime", "Microsoft.UI.Xaml.Media.Animation", "KeyTime" },
                { "KeyTimeHelper" },
                { "RepeatBehavior", "Microsoft.UI.Xaml.Media.Animation", "RepeatBehavior" },
                { "RepeatBehaviorHelper" },
                { "RepeatBehaviorType", "Microsoft.UI.Xaml.Media.Animation", "RepeatBehaviorType" } } },
        { "Microsoft.UI.Xaml.Media.Media3D",
            {
                { "IMatrix3DHelper" },
                { "IMatrix3DHelperStatics" },
                { "Matrix3D", "Microsoft.UI.Xaml.Media.Media3D", "Matrix3D" },
                { "Matrix3DHelper" },
            } },
        { "Windows.Foundation",
            { { "DateTime", "System", "DateTimeOffset", true },
                { "EventHandler`1", "System", "EventHandler", false },
                { "EventRegistrationToken", "WinRT", "EventRegistrationToken", false },
                { "HResult", "System", "Exception", true },
                { "IClosable", "System", "IDisposable", true, true },
                { "IPropertyValue", "Windows.Foundation", "IPropertyValue", true },
                { "IReferenceArray`1", "Windows.Foundation", "IReferenceArray", true },
                { "IReference`1", "System", "Nullable", true },
                { "Point", "Windows.Foundation", "Point" },
                { "Rect", "Windows.Foundation", "Rect" },
                { "Size", "Windows.Foundation", "Size" },
                { "TimeSpan", "System", "TimeSpan", true },
                { "Uri", "System", "Uri", true } } },
        { "Windows.Foundation.Collections",
            {
                { "IIterable`1", "System.Collections.Generic", "IEnumerable`1", true, true },
                { "IIterator`1", "System.Collections.Generic", "IEnumerator`1", true, true },
                { "IKeyValuePair`2", "System.Collections.Generic", "KeyValuePair`2", true },
                { "IMapView`2", "System.Collections.Generic", "IReadOnlyDictionary`2", true, true },
                { "IMap`2", "System.Collections.Generic", "IDictionary`2", true, true },
                { "IVectorView`1", "System.Collections.Generic", "IReadOnlyList`1", true, true },
                { "IVector`1", "System.Collections.Generic", "IList`1", true, true },
            } },
        { "Windows.Foundation.Metadata",
            {
                { "AttributeTargets", "System", "AttributeTargets" },
                { "AttributeUsageAttribute", "System", "AttributeUsageAttribute" },
            } },
        { "Windows.Foundation.Numerics",
            {
                { "Matrix3x2", "System.Numerics", "Matrix3x2" },
                { "Matrix4x4", "System.Numerics", "Matrix4x4" },
                { "Plane", "System.Numerics", "Plane" },
                { "Quaternion", "System.Numerics", "Quaternion" },
                { "Vector2", "System.Numerics", "Vector2" },
                { "Vector3", "System.Numerics", "Vector3" },
                { "Vector4", "System.Numerics", "Vector4" },
            } },
        { "Windows.UI",
            {
                { "Color", "Windows.UI", "Color" },
                { "ColorHelper" },
                { "IColorHelper" },
                { "IColorHelperStatics" },
                { "IColorHelperStatics2" },
            } },
        // Temporary, until WinUI provides TypeName
        { "Windows.UI.Xaml.Interop",
            { { "TypeKind", "Windows.UI.Xaml.Interop", "TypeKind", true },
                { "TypeName", "System", "Type", true } } },
    };

    auto nsItr = std::lower_bound(std::begin(mapped_types), std::end(mapped_types), typeNamespace, [](auto &&v, std::string_view ns) {
        return v.name_space < ns;
    });

    if ((nsItr == std::end(mapped_types)) || (nsItr->name_space != typeNamespace)) {
        return {};
    }

    return nsItr->types;
}

inline const mapped_type *get_mapped_type(std::string_view typeNamespace, std::string_view typeName) {
    auto mapped_types = get_mapped_types_in_namespace(typeNamespace);

    if (mapped_types.size() == 0) {
        return nullptr;
    }

    auto nameItr = std::lower_bound(mapped_types.begin(), mapped_types.end(), typeName, [](auto &&v, std::string_view name) {
        return v.abi_name < name;
    });
    if ((nameItr == mapped_types.end()) || (nameItr->abi_name != typeName)) {
        return nullptr;
    }

    return &*nameItr;
}

enum class typedef_name_type {
    Projected,
    CCW,
    ABI
};

std::string get_mapped_element_type(ElementType elementType) {
    switch (elementType) {
    case ElementType::End:
        return "End";
    case ElementType::Void:
        return "Void";
    case ElementType::Boolean:
        return "Boolean";
    case ElementType::Char:
        return "Char";
    case ElementType::I1:
        return "I1";
    case ElementType::U1:
        return "UI";
    case ElementType::I2:
        return "I2";
    case ElementType::U2:
        return "U2";
    case ElementType::I4:
        return "I4";
    case ElementType::U4:
        return "U4";
    case ElementType::I8:
        return "I8";
    case ElementType::U8:
        return "U8";
    case ElementType::R4:
        return "R4";
    case ElementType::R8:
        return "R8";
    case ElementType::String:
        return "String";
    case ElementType::Ptr:
        return "Ptr";
    case ElementType::ByRef:
        return "ByRef";
    case ElementType::ValueType:
        return "ValueType";
    case ElementType::Class:
        return "Class";
    case ElementType::Var:
        return "Var";
    case ElementType::Array:
        return "Array";
    case ElementType::GenericInst:
        return "GenericInst";
    case ElementType::TypedByRef:
        return "TypedByRef";
    case ElementType::I:
        return "IntPtr";
    case ElementType::U:
        return "UIntPtr";
    case ElementType::FnPtr:
        return "FnPtr";
    case ElementType::Object:
        return "Object";
    case ElementType::SZArray:
        return "SZArray";
    case ElementType::MVar:
        return "MVar";
    case ElementType::CModReqd:
        return "CModReqd";
    case ElementType::CModOpt:
        return "CModOpt";
    case ElementType::Internal:
        return "Internal";
    case ElementType::Modifier:
        return "Modifier";
    case ElementType::Sentinel:
        return "Sentinel";
    case ElementType::Pinned:
        return "Pinned";
    case ElementType::Type:
        return "Type";
    case ElementType::TaggedObject:
        return "TaggedObject";
    case ElementType::Field:
        return "Field";
    case ElementType::Property:
        return "Property";
    case ElementType::Enum:
        return "Enum";
    default:
        return "Unknown";
    }
}

bool are_equal(const winmd::reader::MethodDef &iface_method, winmd::reader::MethodDef &method) {
    if (iface_method.Name() != method.Name()) {
        return false;
    };

    auto iface_ret_type = iface_method.Signature().ReturnType();
    auto ret_type = method.Signature().ReturnType();
    if ((!iface_ret_type && ret_type) || (!ret_type && iface_ret_type)) {
        return false;
    };

    if (ret_type.Type().Type().index() != iface_ret_type.Type().Type().index()) {
        return false;
    };

    if (distance(method.ParamList()) != distance(iface_method.ParamList()))
        return false;

    std::vector<Param> lhs(distance(method.ParamList()));
    std::copy(begin(method.ParamList()), end(method.ParamList()), lhs.begin());
    std::vector<Param> rhs(distance(iface_method.ParamList()));
    std::copy(begin(iface_method.ParamList()), end(iface_method.ParamList()), rhs.begin());

    for (size_t i = 0; i < distance(method.ParamList()); i++) {
        auto it1 = lhs.at(i);
        auto it2 = rhs.at(i);

        if (it1.Name() != it2.Name())
            return false;

        if (it1.Flags().value != it2.Flags().value)
            return false;
    }

    return true;
}

const MethodDef &get_interface_method(TypeDef &parent_type, MethodDef &method, bool &is_static) {
    auto get_system_type = [&](auto &&signature) -> TypeDef {
        for (auto &&arg : signature.FixedArgs()) {
            if (auto type_param = std::get_if<ElemSig::SystemType>(&std::get<ElemSig>(arg.value).value)) {
                return parent_type.get_cache().find_required(type_param->name);
            }
        }

        return {};
    };

    std::vector<TypeDef> ifaces(distance(parent_type.InterfaceImpl()));

    for (auto &&iface : parent_type.InterfaceImpl()) {
        auto iface_type = get_type_semantics(iface.Interface());
        auto iface_type_definition = std::get_if<type_definition>(&iface_type);
        if (iface_type_definition != nullptr)
            ifaces.push_back(parent_type.get_cache().find_required((*iface_type_definition).TypeNamespace(), (*iface_type_definition).TypeName()));
    }

    for (auto &&iface_type_definition : ifaces) {
        if (!iface_type_definition || distance(iface_type_definition.MethodList()) == 0)
            continue;

        for (auto &&iface_method : iface_type_definition.MethodList()) {
            if (!are_equal(iface_method, method))
                continue;

            return iface_method;
        }
    }

    for (auto &&attribute : parent_type.CustomAttribute()) {
        auto attribute_name = attribute.TypeNamespaceAndName();

        if (attribute_name.first != "Windows.Foundation.Metadata") {
            continue;
        }
        if (attribute_name.second == "StaticAttribute") {
            auto iface_type_definition = get_system_type(attribute.Value());
            for (auto &&iface_method : iface_type_definition.MethodList()) {
                if (!are_equal(iface_method, method))
                    continue;

                is_static = true;
                return iface_method;
            }
        }
    }

    return {};
}