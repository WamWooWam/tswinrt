#include "interop.h"

namespace interop {
    resolved_variant_argument::resolved_variant_argument(
        ElementType const type,
        const uint8_t* const value_first,
        const uint8_t* const value_last,
        const uint8_t* const type_name_first,
        const uint8_t* const type_name_last)
        : _type(type),
          _value_first(value_first),
          _value_last(value_last),
          _type_name_first(type_name_first),
          _type_name_last(type_name_last) {
    }

    auto resolved_variant_argument::element_type() const -> ElementType {
        return _type.get();
    }

    auto resolved_variant_argument::logical_type() const -> type_semantics {
        if (_type.get() == ElementType::Class) {
            // First, we see if we have a known type name (i.e. type_name() returns a string).  If
            // we have one, we use that to get the type of the argument.
            core::string_reference const known_type_name(type_name());
            if (!known_type_name.empty()) {
                reflection::type const type(get_type(known_type_name));

                // If the static type of the object was Platform::Object, we'll instead try to use
                // its dynamic type for overload resolution:
                if (type && type != get_type(L"Platform", L"Object"))
                    return type;
            }

            // Otherwise, see if we can get the type from the IInspectable argument:
            core::assert_true([&] { return sizeof(IInspectable*) == core::distance(begin_value(), end_value()); });

            IInspectable* value(nullptr);
            core::range_checked_copy(begin_value(), end_value(), core::begin_bytes(value), core::end_bytes(value));

            // If we have an IInspectable object, try to get its runtime class name:
            if (value != nullptr) {
                utility::smart_hstring inspectable_type_name;
                utility::throw_on_failure(value->GetRuntimeClassName(inspectable_type_name.proxy()));

                reflection::type const type(get_type(inspectable_type_name.c_str()));
                if (type)
                    return type;
            }

            // TODO For nullptr, we should probably allow conversion to any interface with equal
            // conversion rank.  How to do this cleanly, though, is a good question.

            // Finally, fall back to use Platform::Object:
            reflection::type const type(get_type(L"Platform", L"Object"));
            if (type)
                return type;

            // Well, that was our last check; if we still failed to get the type, it's time to throw
            throw core::logic_error(L"failed to find type");
        }
        else if (_type.get() == metadata::element_type::value_type) {
            core::assert_not_yet_implemented();
        }
        else {
            reflection::detail::loader_context const& root(global_package_loader::get()
                                                               .loader()
                                                               .context(core::internal_key()));

            return reflection::type(root.resolve_fundamental_type(_type.get()), core::internal_key());
        }
    }

    auto resolved_variant_argument::begin_value() const -> const uint8_t* {
        return _value_first.get();
    }

    auto resolved_variant_argument::end_value() const -> const uint8_t* {
        return _value_last.get();
    }

    auto resolved_variant_argument::type_name() const -> core::string_reference {
        if (_type_name_first.get() == _type_name_last.get())
            return core::string_reference();

        return core::string_reference(
            reinterpret_cast<core::const_character_iterator>(_type_name_first.get()),
            reinterpret_cast<core::const_character_iterator>(_type_name_last.get()));
    }

}