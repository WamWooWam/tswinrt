#include "interop.h"

namespace interop {
    unresolved_variant_argument::unresolved_variant_argument(ElementType const type,
        uint32_t const value_index,
        uint32_t const value_size,
        uint32_t const type_name_index,
        uint32_t const type_name_size)
        : _type(type),
          _value_index(value_index),
          _value_size(value_size),
          _type_name_index(type_name_index),
          _type_name_size(type_name_size) {
    }

    auto unresolved_variant_argument::element_type() const -> ElementType {
        return _type.get();
    }

    auto unresolved_variant_argument::value_index() const -> uint32_t {
        return _value_index.get();
    }

    auto unresolved_variant_argument::value_size() const -> uint32_t {
        return _value_size.get();
    }

    auto unresolved_variant_argument::type_name_index() const -> uint32_t {
        return _type_name_index.get();
    }

    auto unresolved_variant_argument::type_name_size() const -> uint32_t {
        return _type_name_size.get();
    }
}