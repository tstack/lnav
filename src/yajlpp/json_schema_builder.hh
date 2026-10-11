/**
 * Copyright (c) 2026, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TIMOTHY STACK AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file json_schema_builder.hh
 */

#ifndef json_schema_builder_hh
#define json_schema_builder_hh

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <stdint.h>

#include "base/intern_string.hh"
#include "base/result.h"
#include "yajl/api/yajl_gen.h"

/**
 * Derives a JSON Schema (2020-12) from sample JSON documents.  Each sample is
 * parsed with yajl callbacks and folded into a tree of nodes, one per
 * location in the documents, that records the types that were seen there.
 */
class json_schema_builder {
public:
    static constexpr const char* SCHEMA_URI
        = "https://json-schema.org/draft/2020-12/schema";
    /**
     * The "format" of strings that are numbers with units, like "4.5ms" or
     * "2 KiB", that humanize::try_from() can parse.
     */
    static constexpr const char* MEASURE_FORMAT = "measure";

    struct options {
        /**
         * Describe arrays by position, with "prefixItems", instead of with
         * one "items" schema that covers every element.
         */
        bool o_tuples{false};
        /**
         * Type strings as the JSON literal they spell, if any, for values
         * that were extracted as text: "12" is a number, "true" a boolean,
         * and "null" a null.  A location that also had other strings is
         * just a string.
         */
        bool o_scalar_strings{false};
    };

    /** What was seen at one location in the samples. */
    struct node {
        /** The property name, for a node in an object's n_properties. */
        std::string n_name;
        bool n_null{false};
        bool n_boolean{false};
        bool n_number{false};
        bool n_string{false};
        bool n_object{false};
        bool n_array{false};
        /** Some strings were numbers with units, like "4.5ms". */
        bool n_measure{false};
        /** Some strings were not numbers, with or without units. */
        bool n_non_measure{false};
        /** The first string here, even one typed as a scalar. */
        std::optional<std::string> n_example;
        /** The object properties, in the order they were first seen. */
        std::vector<node> n_properties;
        /** The array elements by position, with options::o_tuples. */
        std::vector<node> n_prefix_items;
        /** The array elements, without options::o_tuples. */
        std::unique_ptr<node> n_items;

        const node* property(string_fragment name) const;
    };

    json_schema_builder() = default;

    explicit json_schema_builder(options opts) : jsb_options(opts) {}

    /**
     * Folds in one sample document, or returns the parse error if it is not
     * valid JSON, in which case nothing is folded in.
     */
    Result<void, std::string> add_sample(string_fragment json);

    /** The number of samples that were folded in. */
    uint32_t sample_count() const { return this->jsb_samples; }

    /** The root of the samples, which is empty until one is added. */
    const node& root() const { return this->jsb_root; }

    /**
     * Generates the schema keywords for a node ("type", "properties", ...) into
     * a map that the caller has already opened, so it can add its own
     * annotations, like a "title".
     */
    void gen_keywords(yajl_gen gen, const node& n) const;

    /** A complete schema document for the samples. */
    std::string to_string() const;

private:
    struct frame {
        node* f_node;
        bool f_array;
        size_t f_index{0};
    };

    friend struct json_schema_builder_callbacks;

    node* next_node();

    options jsb_options;
    uint32_t jsb_samples{0};
    node jsb_root;
    std::vector<frame> jsb_stack;
    std::string jsb_key;
};

#endif
