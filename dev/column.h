#pragma once

#include <tuple>  //  std::tuple
#include <string>  //  std::string
#include <memory>  //  std::unique_ptr
#include <type_traits>  //  std::false_type, std::is_same, std::enable_if

#include "functional/cxx_universal.h"
#include "functional/cxx_polyfill.h"
#include "tuple_helper/tuple_traits.h"
#include "tuple_helper/tuple_filter.h"
#include "type_traits.h"
#include "member_traits/member_traits.h"
#include "type_is_nullable.h"
#include "constraints.h"

namespace sqlite_orm {

    namespace internal {

        struct basic_column {

            /**
             *  Column name. Specified during construction in `make_column()`.
             */
            const std::string name;
        };

        struct empty_setter {};

        template<class G, class S>
        struct field_access_closure {
            using member_pointer_t = G;
            using setter_type = S;
            using object_type = member_object_type_t<G>;
            using field_type = member_field_type_t<G>;

            /**
             *  Member pointer used to read a field value.
             *  If it is a object member pointer it is also used to write a field value.
             */
            const member_pointer_t member_pointer;

            /**
             *  Setter member function to write a field value
             */
            SQLITE_ORM_NOUNIQUEADDRESS
            const setter_type setter;
        };

        template<class... Op>
        struct column_constraints {
            using constraints_type = std::tuple<Op...>;

            constraints_type constraints;
        };

        /**
         *  This class stores information about a single column.
         *  column_t is a pair of [column_name:member_pointer] mapped to a storage.
         *  
         *  O is a mapped class, e.g. User
         *  T is a mapped class'es field type, e.g. &User::name
         *  Op... is a constraints pack, e.g. primary_key_t, autoincrement_t etc
         */
        template<class G, class S, class... Op>
        struct column_t : basic_column, field_access_closure<G, S>, column_constraints<Op...> {
            using field_type = typename field_access_closure<G, S>::field_type;
            using constraints_type = typename column_constraints<Op...>::constraints_type;

#ifndef SQLITE_ORM_AGGREGATE_BASES_SUPPORTED
            column_t(std::string name, G memberPointer, S setter, std::tuple<Op...> op) :
                basic_column{move(name)}, field_access_closure<G, S>{memberPointer, setter}, column_constraints{
                                                                                                 move(op)} {}
#endif

            /**
             *  Checks whether contraints are of trait `Trait`
             */
            template<template<class...> class Trait>
            constexpr bool is() const {
                return tuple_has<Trait, constraints_type>::value;
            }

            /**
             *  Simplified interface for `NOT NULL` constraint
             */
            constexpr bool is_not_null() const {
                return !type_is_nullable<field_type>::value;
            }

            constexpr bool is_generated() const {
#if SQLITE_VERSION_NUMBER >= 3031000
                return is<is_generated_always>();
#else
                return false;
#endif
            }

            /**
             *  Simplified interface for `DEFAULT` constraint
             *  @return string representation of default value if it exists otherwise nullptr
             */
            std::unique_ptr<std::string> default_value() const;
        };

        template<class T>
        SQLITE_ORM_INLINE_VAR constexpr bool is_column_v = polyfill::is_specialization_of_v<T, column_t>;

        template<class T>
        using is_column = polyfill::bool_constant<is_column_v<T>>;

        /**
         *  Column with insertable primary key traits. Common case.
         */
        template<class T, class SFINAE = void>
        struct is_column_with_insertable_primary_key : std::false_type {};

        /**
         *  Column with insertable primary key traits. Specialized case case.
         */
        template<class C>
        struct is_column_with_insertable_primary_key<
            C,
            std::enable_if_t<tuple_has<is_primary_key, typename C::constraints_type>::value>>
            : is_primary_key_insertable<C> {};

        /**
         *  Column with noninsertable primary key traits. Common case.
         */
        template<class T, class SFINAE = void>
        struct is_column_with_noninsertable_primary_key : std::false_type {};

        /**
         *  Column with noninsertable primary key traits. Specialized case case.
         */
        template<class C>
        struct is_column_with_noninsertable_primary_key<
            C,
            std::enable_if_t<tuple_has<is_primary_key, typename C::constraints_type>::value>>
            : polyfill::negation<is_primary_key_insertable<C>> {};

        template<class T>
        struct column_field_type {
            using type = void;
        };

        template<class G, class S, class... Op>
        struct column_field_type<column_t<G, S, Op...>> {
            using type = typename column_t<G, S, Op...>::field_type;
        };

        template<class T>
        using column_field_type_t = typename column_field_type<T>::type;

        template<class T>
        struct column_constraints_type {
            using type = std::tuple<>;
        };

        template<class G, class S, class... Op>
        struct column_constraints_type<column_t<G, S, Op...>> {
            using type = typename column_t<G, S, Op...>::constraints_type;
        };

        template<class T>
        using column_constraints_type_t = typename column_constraints_type<T>::type;

    }

    /**
     *  Column builder function. You should use it to create columns instead of constructor
     */
    template<class M, class... Op, internal::satisfies<std::is_member_object_pointer, M> = true>
    internal::column_t<M, internal::empty_setter, Op...> make_column(std::string name, M m, Op... constraints) {
        static_assert(internal::count_tuple<std::tuple<Op...>, internal::is_constraint>::value ==
                          std::tuple_size<std::tuple<Op...>>::value,
                      "Incorrect constraints pack");
        SQLITE_ORM_CLANG_SUPPRESS_MISSING_BRACES(return {move(name), m, {}, std::make_tuple(constraints...)});
    }

    /**
     *  Column builder function with setter and getter. You should use it to create columns instead of constructor
     */
    template<class G,
             class S,
             class... Op,
             internal::satisfies<internal::is_getter, G> = true,
             internal::satisfies<internal::is_setter, S> = true>
    internal::column_t<G, S, Op...> make_column(std::string name, S setter, G getter, Op... constraints) {
        static_assert(std::is_same<internal::setter_field_type_t<S>, internal::getter_field_type_t<G>>::value,
                      "Getter and setter must get and set same data type");
        static_assert(internal::count_tuple<std::tuple<Op...>, internal::is_constraint>::value ==
                          std::tuple_size<std::tuple<Op...>>::value,
                      "Incorrect constraints pack");
        SQLITE_ORM_CLANG_SUPPRESS_MISSING_BRACES(return {move(name), getter, setter, std::make_tuple(constraints...)});
    }

    /**
     *  Column builder function with getter and setter (reverse order). You should use it to create columns instead of
     * constructor
     */
    template<class G,
             class S,
             class... Op,
             internal::satisfies<internal::is_getter, G> = true,
             internal::satisfies<internal::is_setter, S> = true>
    internal::column_t<G, S, Op...> make_column(std::string name, G getter, S setter, Op... constraints) {
        static_assert(std::is_same<internal::setter_field_type_t<S>, internal::getter_field_type_t<G>>::value,
                      "Getter and setter must get and set same data type");
        static_assert(internal::count_tuple<std::tuple<Op...>, internal::is_constraint>::value ==
                          std::tuple_size<std::tuple<Op...>>::value,
                      "Incorrect constraints pack");
        SQLITE_ORM_CLANG_SUPPRESS_MISSING_BRACES(return {move(name), getter, setter, std::make_tuple(constraints...)});
    }
}
