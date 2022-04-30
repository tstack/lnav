// Copyright (c) 2017-2022, University of Cincinnati, developed by Henry Schreiner
// under NSF AWARD 1414736 and by the respective contributors.
// All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// [CLI11:public_includes:set]
#include <algorithm>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
// [CLI11:public_includes:end]

#include "Error.hpp"
#include "Macros.hpp"
#include "Split.hpp"
#include "StringTools.hpp"
#include "Validators.hpp"

namespace CLI {
// [CLI11:option_hpp:verbatim]

using results_t = std::vector<std::string>;
/// callback function definition
using callback_t = std::function<bool(const results_t &)>;

class Option;
class App;

using Option_p = std::unique_ptr<Option>;
/// Enumeration of the multiOption Policy selection
enum class MultiOptionPolicy : char {
    Throw,      //!< Throw an error if any extra arguments were given
    TakeLast,   //!< take only the last Expected number of arguments
    TakeFirst,  //!< take only the first Expected number of arguments
    Join,       //!< merge all the arguments together into a single string via the delimiter character default('\n')
    TakeAll,    //!< just get all the passed argument regardless
    Sum         //!< sum all the arguments together if numerical or concatenate directly without delimiter
};

/// This is the CRTP base class for Option and OptionDefaults. It was designed this way
/// to share parts of the class; an OptionDefaults can copy to an Option.
template <typename CRTP> class OptionBase {
    friend App;

  protected:
    /// The group membership
    std::string group_ = std::string("Options");

    /// True if this is a required option
    bool required_{false};

    /// Ignore the case when matching (option, not value)
    bool ignore_case_{false};

    /// Ignore underscores when matching (option, not value)
    bool ignore_underscore_{false};

    /// Allow this option to be given in a configuration file
    bool configurable_{true};

    /// Disable overriding flag values with '=value'
    bool disable_flag_override_{false};

    /// Specify a delimiter character for vector arguments
    char delimiter_{'\0'};

    /// Automatically capture default value
    bool always_capture_default_{false};

    /// Policy for handling multiple arguments beyond the expected Max
    MultiOptionPolicy multi_option_policy_{MultiOptionPolicy::Throw};

    /// Copy the contents to another similar class (one based on OptionBase)
    template <typename T> void copy_to(T *other) const {
        other->group(group_);
        other->required(required_);
        other->ignore_case(ignore_case_);
        other->ignore_underscore(ignore_underscore_);
        other->configurable(configurable_);
        other->disable_flag_override(disable_flag_override_);
        other->delimiter(delimiter_);
        other->always_capture_default(always_capture_default_);
        other->multi_option_policy(multi_option_policy_);
    }

  public:
    // setters

    /// Changes the group membership
    CRTP *group(const std::string &name) {
        if(!detail::valid_alias_name_string(name)) {
            throw IncorrectConstruction("Group names may not contain newlines or null characters");
        }
        group_ = name;
        return static_cast<CRTP *>(this);
    }

    /// Set the option as required
    CRTP *required(bool value = true) {
        required_ = value;
        return static_cast<CRTP *>(this);
    }

    /// Support Plumbum term
    CRTP *mandatory(bool value = true) { return required(value); }

    CRTP *always_capture_default(bool value = true) {
        always_capture_default_ = value;
        return static_cast<CRTP *>(this);
    }

    // Getters

    /// Get the group of this option
    const std::string &get_group() const { return group_; }

    /// True if this is a required option
    bool get_required() const { return required_; }

    /// The status of ignore case
    bool get_ignore_case() const { return ignore_case_; }

    /// The status of ignore_underscore
    bool get_ignore_underscore() const { return ignore_underscore_; }

    /// The status of configurable
    bool get_configurable() const { return configurable_; }

    /// The status of configurable
    bool get_disable_flag_override() const { return disable_flag_override_; }

    /// Get the current delimiter char
    char get_delimiter() const { return delimiter_; }

    /// Return true if this will automatically capture the default value for help printing
    bool get_always_capture_default() const { return always_capture_default_; }

    /// The status of the multi option policy
    MultiOptionPolicy get_multi_option_policy() const { return multi_option_policy_; }

    // Shortcuts for multi option policy

    /// Set the multi option policy to take last
    CRTP *take_last() {
        auto self = static_cast<CRTP *>(this);
        self->multi_option_policy(MultiOptionPolicy::TakeLast);
        return self;
    }

    /// Set the multi option policy to take last
    CRTP *take_first() {
        auto self = static_cast<CRTP *>(this);
        self->multi_option_policy(MultiOptionPolicy::TakeFirst);
        return self;
    }

    /// Set the multi option policy to take all arguments
    CRTP *take_all() {
        auto self = static_cast<CRTP *>(this);
        self->multi_option_policy(MultiOptionPolicy::TakeAll);
        return self;
    }

    /// Set the multi option policy to join
    CRTP *join() {
        auto self = static_cast<CRTP *>(this);
        self->multi_option_policy(MultiOptionPolicy::Join);
        return self;
    }

    /// Set the multi option policy to join with a specific delimiter
    CRTP *join(char delim) {
        auto self = static_cast<CRTP *>(this);
        self->delimiter_ = delim;
        self->multi_option_policy(MultiOptionPolicy::Join);
        return self;
    }

    /// Allow in a configuration file
    CRTP *configurable(bool value = true) {
        configurable_ = value;
        return static_cast<CRTP *>(this);
    }

    /// Allow in a configuration file
    CRTP *delimiter(char value = '\0') {
        delimiter_ = value;
        return static_cast<CRTP *>(this);
    }
};

/// This is a version of OptionBase that only supports setting values,
/// for defaults. It is stored as the default option in an App.
class OptionDefaults : public OptionBase<OptionDefaults> {
  public:
    OptionDefaults() = default;

    // Methods here need a different implementation if they are Option vs. OptionDefault

    /// Take the last argument if given multiple times
    OptionDefaults *multi_option_policy(MultiOptionPolicy value = MultiOptionPolicy::Throw) {
        multi_option_policy_ = value;
        return this;
    }

    /// Ignore the case of the option name
    OptionDefaults *ignore_case(bool value = true) {
        ignore_case_ = value;
        return this;
    }

    /// Ignore underscores in the option name
    OptionDefaults *ignore_underscore(bool value = true) {
        ignore_underscore_ = value;
        return this;
    }

    /// Disable overriding flag values with an '=<value>' segment
    OptionDefaults *disable_flag_override(bool value = true) {
        disable_flag_override_ = value;
        return this;
    }

    /// set a delimiter character to split up single arguments to treat as multiple inputs
    OptionDefaults *delimiter(char value = '\0') {
        delimiter_ = value;
        return this;
    }
};

class Option : public OptionBase<Option> {
    friend App;

  protected:
    /// @name Names
    ///@{

    /// A list of the short names (`-a`) without the leading dashes
    std::vector<std::string> snames_{};

    /// A list of the long names (`--long`) without the leading dashes
    std::vector<std::string> lnames_{};

    /// A list of the flag names with the appropriate default value, the first part of the pair should be duplicates of
    /// what is in snames or lnames but will trigger a particular response on a flag
    std::vector<std::pair<std::string, std::string>> default_flag_values_{};

    /// a list of flag names with specified default values;
    std::vector<std::string> fnames_{};

    /// A positional name
    std::string pname_{};

    /// If given, check the environment for this option
    std::string envname_{};

    ///@}
    /// @name Help
    ///@{

    /// The description for help strings
    std::string description_{};

    /// A human readable default value, either manually set, captured, or captured by default
    std::string default_str_{};

    /// If given, replace the text that describes the option type and usage in the help text
    std::string option_text_{};

    /// A human readable type value, set when App creates this
    ///
    /// This is a lambda function so "types" can be dynamic, such as when a set prints its contents.
    std::function<std::string()> type_name_{[]() { return std::string(); }};

    /// Run this function to capture a default (ignore if empty)
    std::function<std::string()> default_function_{};

    ///@}
    /// @name Configuration
    ///@{

    /// The number of arguments that make up one option. max is the nominal type size, min is the minimum number of
    /// strings
    int type_size_max_{1};
    /// The minimum number of arguments an option should be expecting
    int type_size_min_{1};

    /// The minimum number of expected values
    int expected_min_{1};
    /// The maximum number of expected values
    int expected_max_{1};

    /// A list of Validators to run on each value parsed
    std::vector<Validator> validators_{};

    /// A list of options that are required with this option
    std::set<Option *> needs_{};

    /// A list of options that are excluded with this option
    std::set<Option *> excludes_{};

    ///@}
    /// @name Other
    ///@{

    /// link back up to the parent App for fallthrough
    App *parent_{nullptr};

    /// Options store a callback to do all the work
    callback_t callback_{};

    ///@}
    /// @name Parsing results
    ///@{

    /// complete Results of parsing
    results_t results_{};
    /// results after reduction
    results_t proc_results_{};
    /// enumeration for the option state machine
    enum class option_state : char {
        parsing = 0,       //!< The option is currently collecting parsed results
        validated = 2,     //!< the results have been validated
        reduced = 4,       //!< a subset of results has been generated
        callback_run = 6,  //!< the callback has been executed
    };
    /// Whether the callback has run (needed for INI parsing)
    option_state current_option_state_{option_state::parsing};
    /// Specify that extra args beyond type_size_max should be allowed
    bool allow_extra_args_{false};
    /// Specify that the option should act like a flag vs regular option
    bool flag_like_{false};
    /// Control option to run the callback to set the default
    bool run_callback_for_default_{false};
    /// flag indicating a separator needs to be injected after each argument call
    bool inject_separator_{false};
    /// flag indicating that the option should trigger the validation and callback chain on each result when loaded
    bool trigger_on_result_{false};
    /// flag indicating that the option should force the callback regardless if any results present
    bool force_callback_{false};
    ///@}

    /// Making an option by hand is not defined, it must be made by the App class
    Option(std::string option_name, std::string option_description, callback_t callback, App *parent)
        : description_(std::move(option_description)), parent_(parent), callback_(std::move(callback)) {
        std::tie(snames_, lnames_, pname_) = detail::get_names(detail::split_names(option_name));
    }

  public:
    /// @name Basic
    ///@{

    Option(const Option &) = delete;
    Option &operator=(const Option &) = delete;

    /// Count the total number of times an option was passed
    std::size_t count() const { return results_.size(); }

    /// True if the option was not passed
    bool empty() const { return results_.empty(); }

    /// This bool operator returns true if any arguments were passed or the option callback is forced
    explicit operator bool() const { return !empty() || force_callback_; }

    /// Clear the parsed results (mostly for testing)
    void clear() {
        results_.clear();
        current_option_state_ = option_state::parsing;
    }

    ///@}
    /// @name Setting options
    ///@{

    /// Set the number of expected arguments
    Option *expected(int value) {
        if(value < 0) {
            expected_min_ = -value;
            if(expected_max_ < expected_min_) {
                expected_max_ = expected_min_;
            }
            allow_extra_args_ = true;
            flag_like_ = false;
        } else if(value == detail::expected_max_vector_size) {
            expected_min_ = 1;
            expected_max_ = detail::expected_max_vector_size;
            allow_extra_args_ = true;
            flag_like_ = false;
        } else {
            expected_min_ = value;
            expected_max_ = value;
            flag_like_ = (expected_min_ == 0);
        }
        return this;
    }

    /// Set the range of expected arguments
    Option *expected(int value_min, int value_max) {
        if(value_min < 0) {
            value_min = -value_min;
        }

        if(value_max < 0) {
            value_max = detail::expected_max_vector_size;
        }
        if(value_max < value_min) {
            expected_min_ = value_max;
            expected_max_ = value_min;
        } else {
            expected_max_ = value_max;
            expected_min_ = value_min;
        }

        return this;
    }
    /// Set the value of allow_extra_args which allows extra value arguments on the flag or option to be included
    /// with each instance
    Option *allow_extra_args(bool value = true) {
        allow_extra_args_ = value;
        return this;
    }
    /// Get the current value of allow extra args
    bool get_allow_extra_args() const { return allow_extra_args_; }
    /// Set the value of trigger_on_parse which specifies that the option callback should be triggered on every parse
    Option *trigger_on_parse(bool value = true) {
        trigger_on_result_ = value;
        return this;
    }
    /// The status of trigger on parse
    bool get_trigger_on_parse() const { return trigger_on_result_; }

    /// Set the value of force_callback
    Option *force_callback(bool value = true) {
        force_callback_ = value;
        return this;
    }
    /// The status of force_callback
    bool get_force_callback() const { return force_callback_; }

    /// Set the value of run_callback_for_default which controls whether the callback function should be called to set
    /// the default This is controlled automatically but could be manipulated by the user.
    Option *run_callback_for_default(bool value = true) {
        run_callback_for_default_ = value;
        return this;
    }
    /// Get the current value of run_callback_for_default
    bool get_run_callback_for_default() const { return run_callback_for_default_; }

    /// Adds a Validator with a built in type name
    Option *check(Validator validator, const std::string &validator_name = "") {
        validator.non_modifying();
        validators_.push_back(std::move(validator));
        if(!validator_name.empty())
            validators_.back().name(validator_name);
        return this;
    }

    /// Adds a Validator. Takes a const string& and returns an error message (empty if conversion/check is okay).
    Option *check(std::function<std::string(const std::string &)> Validator,
                  std::string Validator_description = "",
                  std::string Validator_name = "") {
        validators_.emplace_back(Validator, std::move(Validator_description), std::move(Validator_name));
        validators_.back().non_modifying();
        return this;
    }

    /// Adds a transforming Validator with a built in type name
    Option *transform(Validator Validator, const std::string &Validator_name = "") {
        validators_.insert(validators_.begin(), std::move(Validator));
        if(!Validator_name.empty())
            validators_.front().name(Validator_name);
        return this;
    }

    /// Adds a Validator-like function that can change result
    Option *transform(const std::function<std::string(std::string)> &func,
                      std::string transform_description = "",
                      std::string transform_name = "") {
        validators_.insert(validators_.begin(),
                           Validator(
                               [func](std::string &val) {
                                   val = func(val);
                                   return std::string{};
                               },
                               std::move(transform_description),
                               std::move(transform_name)));

        return this;
    }

    /// Adds a user supplied function to run on each item passed in (communicate though lambda capture)
    Option *each(const std::function<void(std::string)> &func) {
        validators_.emplace_back(
            [func](std::string &inout) {
                func(inout);
                return std::string{};
            },
            std::string{});
        return this;
    }
    /// Get a named Validator
    Validator *get_validator(const std::string &Validator_name = "") {
        for(auto &Validator : validators_) {
            if(Validator_name == Validator.get_name()) {
                return &Validator;
            }
        }
        if((Validator_name.empty()) && (!validators_.empty())) {
            return &(validators_.front());
        }
        throw OptionNotFound(std::string{"Validator "} + Validator_name + " Not Found");
    }

    /// Get a Validator by index NOTE: this may not be the order of definition
    Validator *get_validator(int index) {
        // This is an signed int so that it is not equivalent to a pointer.
        if(index >= 0 && index < static_cast<int>(validators_.size())) {
            return &(validators_[static_cast<decltype(validators_)::size_type>(index)]);
        }
        throw OptionNotFound("Validator index is not valid");
    }

    /// Sets required options
    Option *needs(Option *opt) {
        if(opt != this) {
            needs_.insert(opt);
        }
        return this;
    }

    /// Can find a string if needed
    template <typename T = App> Option *needs(std::string opt_name) {
        auto opt = static_cast<T *>(parent_)->get_option_no_throw(opt_name);
        if(opt == nullptr) {
            throw IncorrectConstruction::MissingOption(opt_name);
        }
        return needs(opt);
    }

    /// Any number supported, any mix of string and Opt
    template <typename A, typename B, typename... ARG> Option *needs(A opt, B opt1, ARG... args) {
        needs(opt);
        return needs(opt1, args...);
    }

    /// Remove needs link from an option. Returns true if the option really was in the needs list.
    bool remove_needs(Option *opt) {
        auto iterator = std::find(std::begin(needs_), std::end(needs_), opt);

        if(iterator == std::end(needs_)) {
            return false;
        }
        needs_.erase(iterator);
        return true;
    }

    /// Sets excluded options
    Option *excludes(Option *opt) {
        if(opt == this) {
            throw(IncorrectConstruction("and option cannot exclude itself"));
        }
        excludes_.insert(opt);

        // Help text should be symmetric - excluding a should exclude b
        opt->excludes_.insert(this);

        // Ignoring the insert return value, excluding twice is now allowed.
        // (Mostly to allow both directions to be excluded by user, even though the library does it for you.)

        return this;
    }

    /// Can find a string if needed
    template <typename T = App> Option *excludes(std::string opt_name) {
        auto opt = static_cast<T *>(parent_)->get_option_no_throw(opt_name);
        if(opt == nullptr) {
            throw IncorrectConstruction::MissingOption(opt_name);
        }
        return excludes(opt);
    }

    /// Any number supported, any mix of string and Opt
    template <typename A, typename B, typename... ARG> Option *excludes(A opt, B opt1, ARG... args) {
        excludes(opt);
        return excludes(opt1, args...);
    }

    /// Remove needs link from an option. Returns true if the option really was in the needs list.
    bool remove_excludes(Option *opt) {
        auto iterator = std::find(std::begin(excludes_), std::end(excludes_), opt);

        if(iterator == std::end(excludes_)) {
            return false;
        }
        excludes_.erase(iterator);
        return true;
    }

    /// Sets environment variable to read if no option given
    Option *envname(std::string name) {
        envname_ = std::move(name);
        return this;
    }

    /// Ignore case
    ///
    /// The template hides the fact that we don't have the definition of App yet.
    /// You are never expected to add an argument to the template here.
    template <typename T = App> Option *ignore_case(bool value = true) {
        if(!ignore_case_ && value) {
            ignore_case_ = value;
            auto *parent = static_cast<T *>(parent_);
            for(const Option_p &opt : parent->options_) {
                if(opt.get() == this) {
                    continue;
                }
                auto &omatch = opt->matching_name(*this);
                if(!omatch.empty()) {
                    ignore_case_ = false;
                    throw OptionAlreadyAdded("adding ignore case caused a name conflict with " + omatch);
                }
            }
        } else {
            ignore_case_ = value;
        }
        return this;
    }

    /// Ignore underscores in the option names
    ///
    /// The template hides the fact that we don't have the definition of App yet.
    /// You are never expected to add an argument to the template here.
    template <typename T = App> Option *ignore_underscore(bool value = true) {

        if(!ignore_underscore_ && value) {
            ignore_underscore_ = value;
            auto *parent = static_cast<T *>(parent_);
            for(const Option_p &opt : parent->options_) {
                if(opt.get() == this) {
                    continue;
                }
                auto &omatch = opt->matching_name(*this);
                if(!omatch.empty()) {
                    ignore_underscore_ = false;
                    throw OptionAlreadyAdded("adding ignore underscore caused a name conflict with " + omatch);
                }
            }
        } else {
            ignore_underscore_ = value;
        }
        return this;
    }

    /// Take the last argument if given multiple times (or another policy)
    Option *multi_option_policy(MultiOptionPolicy value = MultiOptionPolicy::Throw) {
        if(value != multi_option_policy_) {
            if(multi_option_policy_ == MultiOptionPolicy::Throw && expected_max_ == detail::expected_max_vector_size &&
               expected_min_ > 1) {  // this bizarre condition is to maintain backwards compatibility
                                     // with the previous behavior of expected_ with vectors
                expected_max_ = expected_min_;
            }
            multi_option_policy_ = value;
            current_option_state_ = option_state::parsing;
        }
        return this;
    }

    /// Disable flag overrides values, e.g. --flag=<value> is not allowed
    Option *disable_flag_override(bool value = true) {
        disable_flag_override_ = value;
        return this;
    }
    ///@}
    /// @name Accessors
    ///@{

    /// The number of arguments the option expects
    int get_type_size() const { return type_size_min_; }

    /// The minimum number of arguments the option expects
    int get_type_size_min() const { return type_size_min_; }
    /// The maximum number of arguments the option expects
    int get_type_size_max() const { return type_size_max_; }

    /// Return the inject_separator flag
    int get_inject_separator() const { return inject_separator_; }

    /// The environment variable associated to this value
    std::string get_envname() const { return envname_; }

    /// The set of options needed
    std::set<Option *> get_needs() const { return needs_; }

    /// The set of options excluded
    std::set<Option *> get_excludes() const { return excludes_; }

    /// The default value (for help printing)
    std::string get_default_str() const { return default_str_; }

    /// Get the callback function
    callback_t get_callback() const { return callback_; }

    /// Get the long names
    const std::vector<std::string> &get_lnames() const { return lnames_; }

    /// Get the short names
    const std::vector<std::string> &get_snames() const { return snames_; }

    /// Get the flag names with specified default values
    const std::vector<std::string> &get_fnames() const { return fnames_; }
    /// Get a single name for the option, first of lname, pname, sname, envname
    const std::string &get_single_name() const {
        if(!lnames_.empty()) {
            return lnames_[0];
        }
        if(!pname_.empty()) {
            return pname_;
        }
        if(!snames_.empty()) {
            return snames_[0];
        }
        return envname_;
    }
    /// The number of times the option expects to be included
    int get_expected() const { return expected_min_; }

    /// The number of times the option expects to be included
    int get_expected_min() const { return expected_min_; }
    /// The max number of times the option expects to be included
    int get_expected_max() const { return expected_max_; }

    /// The total min number of expected  string values to be used
    int get_items_expected_min() const { return type_size_min_ * expected_min_; }

    /// Get the maximum number of items expected to be returned and used for the callback
    int get_items_expected_max() const {
        int t = type_size_max_;
        return detail::checked_multiply(t, expected_max_) ? t : detail::expected_max_vector_size;
    }
    /// The total min number of expected  string values to be used
    int get_items_expected() const { return get_items_expected_min(); }

    /// True if the argument can be given directly
    bool get_positional() const { return pname_.length() > 0; }

    /// True if option has at least one non-positional name
    bool nonpositional() const { return (snames_.size() + lnames_.size()) > 0; }

    /// True if option has description
    bool has_description() const { return description_.length() > 0; }

    /// Get the description
    const std::string &get_description() const { return description_; }

    /// Set the description
    Option *description(std::string option_description) {
        description_ = std::move(option_description);
        return this;
    }

    Option *option_text(std::string text) {
        option_text_ = std::move(text);
        return this;
    }

    const std::string &get_option_text() const { return option_text_; }

    ///@}
    /// @name Help tools
    ///@{

    /// \brief Gets a comma separated list of names.
    /// Will include / prefer the positional name if positional is true.
    /// If all_options is false, pick just the most descriptive name to show.
    /// Use `get_name(true)` to get the positional name (replaces `get_pname`)
    std::string get_name(bool positional = false,  ///< Show the positional name
                         bool all_options = false  ///< Show every option
    ) const {
        if(get_group().empty())
            return {};  // Hidden

        if(all_options) {

            std::vector<std::string> name_list;

            /// The all list will never include a positional unless asked or that's the only name.
            if((positional && (!pname_.empty())) || (snames_.empty() && lnames_.empty())) {
                name_list.push_back(pname_);
            }
            if((get_items_expected() == 0) && (!fnames_.empty())) {
                for(const std::string &sname : snames_) {
                    name_list.push_back("-" + sname);
                    if(check_fname(sname)) {
                        name_list.back() += "{" + get_flag_value(sname, "") + "}";
                    }
                }

                for(const std::string &lname : lnames_) {
                    name_list.push_back("--" + lname);
                    if(check_fname(lname)) {
                        name_list.back() += "{" + get_flag_value(lname, "") + "}";
                    }
                }
            } else {
                for(const std::string &sname : snames_)
                    name_list.push_back("-" + sname);

                for(const std::string &lname : lnames_)
                    name_list.push_back("--" + lname);
            }

            return detail::join(name_list);
        }

        // This returns the positional name no matter what
        if(positional)
            return pname_;

        // Prefer long name
        if(!lnames_.empty())
            return std::string(2, '-') + lnames_[0];

        // Or short name if no long name
        if(!snames_.empty())
            return std::string(1, '-') + snames_[0];

        // If positional is the only name, it's okay to use that
        return pname_;
    }

    ///@}
    /// @name Parser tools
    ///@{

    /// Process the callback
    void run_callback() {
        if(force_callback_ && results_.empty()) {
            add_result(default_str_);
        }
        if(current_option_state_ == option_state::parsing) {
            _validate_results(results_);
            current_option_state_ = option_state::validated;
        }

        if(current_option_state_ < option_state::reduced) {
            _reduce_results(proc_results_, results_);
            current_option_state_ = option_state::reduced;
        }
        if(current_option_state_ >= option_state::reduced) {
            current_option_state_ = option_state::callback_run;
            if(!(callback_)) {
                return;
            }
            const results_t &send_results = proc_results_.empty() ? results_ : proc_results_;
            bool local_result = callback_(send_results);

            if(!local_result)
                throw ConversionError(get_name(), results_);
        }
    }

    /// If options share any of the same names, find it
    const std::string &matching_name(const Option &other) const {
        static const std::string estring;
        for(const std::string &sname : snames_)
            if(other.check_sname(sname))
                return sname;
        for(const std::string &lname : lnames_)
            if(other.check_lname(lname))
                return lname;

        if(ignore_case_ ||
           ignore_underscore_) {  // We need to do the inverse, in case we are ignore_case or ignore underscore
            for(const std::string &sname : other.snames_)
                if(check_sname(sname))
                    return sname;
            for(const std::string &lname : other.lnames_)
                if(check_lname(lname))
                    return lname;
        }
        return estring;
    }
    /// If options share any of the same names, they are equal (not counting positional)
    bool operator==(const Option &other) const { return !matching_name(other).empty(); }

    /// Check a name. Requires "-" or "--" for short / long, supports positional name
    bool check_name(const std::string &name) const {

        if(name.length() > 2 && name[0] == '-' && name[1] == '-')
            return check_lname(name.substr(2));
        if(name.length() > 1 && name.front() == '-')
            return check_sname(name.substr(1));
        if(!pname_.empty()) {
            std::string local_pname = pname_;
            std::string local_name = name;
            if(ignore_underscore_) {
                local_pname = detail::remove_underscore(local_pname);
                local_name = detail::remove_underscore(local_name);
            }
            if(ignore_case_) {
                local_pname = detail::to_lower(local_pname);
                local_name = detail::to_lower(local_name);
            }
            if(local_name == local_pname) {
                return true;
            }
        }

        if(!envname_.empty()) {
            // this needs to be the original since envname_ shouldn't match on case insensitivity
            return (name == envname_);
        }
        return false;
    }

    /// Requires "-" to be removed from string
    bool check_sname(std::string name) const {
        return (detail::find_member(std::move(name), snames_, ignore_case_) >= 0);
    }

    /// Requires "--" to be removed from string
    bool check_lname(std::string name) const {
        return (detail::find_member(std::move(name), lnames_, ignore_case_, ignore_underscore_) >= 0);
    }

    /// Requires "--" to be removed from string
    bool check_fname(std::string name) const {
        if(fnames_.empty()) {
            return false;
        }
        return (detail::find_member(std::move(name), fnames_, ignore_case_, ignore_underscore_) >= 0);
    }

    /// Get the value that goes for a flag, nominally gets the default value but allows for overrides if not
    /// disabled
    std::string get_flag_value(const std::string &name, std::string input_value) const {
        static const std::string trueString{"true"};
        static const std::string falseString{"false"};
        static const std::string emptyString{"{}"};
        // check for disable flag override_
        if(disable_flag_override_) {
            if(!((input_value.empty()) || (input_value == emptyString))) {
                auto default_ind = detail::find_member(name, fnames_, ignore_case_, ignore_underscore_);
                if(default_ind >= 0) {
                    // We can static cast this to std::size_t because it is more than 0 in this block
                    if(default_flag_values_[static_cast<std::size_t>(default_ind)].second != input_value) {
                        throw(ArgumentMismatch::FlagOverride(name));
                    }
                } else {
                    if(input_value != trueString) {
                        throw(ArgumentMismatch::FlagOverride(name));
                    }
                }
            }
        }
        auto ind = detail::find_member(name, fnames_, ignore_case_, ignore_underscore_);
        if((input_value.empty()) || (input_value == emptyString)) {
            if(flag_like_) {
                return (ind < 0) ? trueString : default_flag_values_[static_cast<std::size_t>(ind)].second;
            } else {
                return (ind < 0) ? default_str_ : default_flag_values_[static_cast<std::size_t>(ind)].second;
            }
        }
        if(ind < 0) {
            return input_value;
        }
        if(default_flag_values_[static_cast<std::size_t>(ind)].second == falseString) {
            try {
                auto val = detail::to_flag_value(input_value);
                return (val == 1) ? falseString : (val == (-1) ? trueString : std::to_string(-val));
            } catch(const std::invalid_argument &) {
                return input_value;
            }
        } else {
            return input_value;
        }
    }

    /// Puts a result at the end
    Option *add_result(std::string s) {
        _add_result(std::move(s), results_);
        current_option_state_ = option_state::parsing;
        return this;
    }

    /// Puts a result at the end and get a count of the number of arguments actually added
    Option *add_result(std::string s, int &results_added) {
        results_added = _add_result(std::move(s), results_);
        current_option_state_ = option_state::parsing;
        return this;
    }

    /// Puts a result at the end
    Option *add_result(std::vector<std::string> s) {
        current_option_state_ = option_state::parsing;
        for(auto &str : s) {
            _add_result(std::move(str), results_);
        }
        return this;
    }

    /// Get the current complete results set
    const results_t &results() const { return results_; }

    /// Get a copy of the results
    results_t reduced_results() const {
        results_t res = proc_results_.empty() ? results_ : proc_results_;
        if(current_option_state_ < option_state::reduced) {
            if(current_option_state_ == option_state::parsing) {
                res = results_;
                _validate_results(res);
            }
            if(!res.empty()) {
                results_t extra;
                _reduce_results(extra, res);
                if(!extra.empty()) {
                    res = std::move(extra);
                }
            }
        }
        return res;
    }

    /// Get the results as a specified type
    template <typename T> void results(T &output) const {
        bool retval;
        if(current_option_state_ >= option_state::reduced || (results_.size() == 1 && validators_.empty())) {
            const results_t &res = (proc_results_.empty()) ? results_ : proc_results_;
            retval = detail::lexical_conversion<T, T>(res, output);
        } else {
            results_t res;
            if(results_.empty()) {
                if(!default_str_.empty()) {
                    // _add_results takes an rvalue only
                    _add_result(std::string(default_str_), res);
                    _validate_results(res);
                    results_t extra;
                    _reduce_results(extra, res);
                    if(!extra.empty()) {
                        res = std::move(extra);
                    }
                } else {
                    res.emplace_back();
                }
            } else {
                res = reduced_results();
            }
            retval = detail::lexical_conversion<T, T>(res, output);
        }
        if(!retval) {
            throw ConversionError(get_name(), results_);
        }
    }

    /// Return the results as the specified type
    template <typename T> T as() const {
        T output;
        results(output);
        return output;
    }

    /// See if the callback has been run already
    bool get_callback_run() const { return (current_option_state_ == option_state::callback_run); }

    ///@}
    /// @name Custom options
    ///@{

    /// Set the type function to run when displayed on this option
    Option *type_name_fn(std::function<std::string()> typefun) {
        type_name_ = std::move(typefun);
        return this;
    }

    /// Set a custom option typestring
    Option *type_name(std::string typeval) {
        type_name_fn([typeval]() { return typeval; });
        return this;
    }

    /// Set a custom option size
    Option *type_size(int option_type_size) {
        if(option_type_size < 0) {
            // this section is included for backwards compatibility
            type_size_max_ = -option_type_size;
            type_size_min_ = -option_type_size;
            expected_max_ = detail::expected_max_vector_size;
        } else {
            type_size_max_ = option_type_size;
            if(type_size_max_ < detail::expected_max_vector_size) {
                type_size_min_ = option_type_size;
            } else {
                inject_separator_ = true;
            }
            if(type_size_max_ == 0)
                required_ = false;
        }
        return this;
    }
    /// Set a custom option type size range
    Option *type_size(int option_type_size_min, int option_type_size_max) {
        if(option_type_size_min < 0 || option_type_size_max < 0) {
            // this section is included for backwards compatibility
            expected_max_ = detail::expected_max_vector_size;
            option_type_size_min = (std::abs)(option_type_size_min);
            option_type_size_max = (std::abs)(option_type_size_max);
        }

        if(option_type_size_min > option_type_size_max) {
            type_size_max_ = option_type_size_min;
            type_size_min_ = option_type_size_max;
        } else {
            type_size_min_ = option_type_size_min;
            type_size_max_ = option_type_size_max;
        }
        if(type_size_max_ == 0) {
            required_ = false;
        }
        if(type_size_max_ >= detail::expected_max_vector_size) {
            inject_separator_ = true;
        }
        return this;
    }

    /// Set the value of the separator injection flag
    void inject_separator(bool value = true) { inject_separator_ = value; }

    /// Set a capture function for the default. Mostly used by App.
    Option *default_function(const std::function<std::string()> &func) {
        default_function_ = func;
        return this;
    }

    /// Capture the default value from the original value (if it can be captured)
    Option *capture_default_str() {
        if(default_function_) {
            default_str_ = default_function_();
        }
        return this;
    }

    /// Set the default value string representation (does not change the contained value)
    Option *default_str(std::string val) {
        default_str_ = std::move(val);
        return this;
    }

    /// Set the default value and validate the results and run the callback if appropriate to set the value into the
    /// bound value only available for types that can be converted to a string
    template <typename X> Option *default_val(const X &val) {
        std::string val_str = detail::to_string(val);
        auto old_option_state = current_option_state_;
        results_t old_results{std::move(results_)};
        results_.clear();
        try {
            add_result(val_str);
            // if trigger_on_result_ is set the callback already ran
            if(run_callback_for_default_ && !trigger_on_result_) {
                run_callback();  // run callback sets the state, we need to reset it again
                current_option_state_ = option_state::parsing;
            } else {
                _validate_results(results_);
                current_option_state_ = old_option_state;
            }
        } catch(const CLI::Error &) {
            // this should be done
            results_ = std::move(old_results);
            current_option_state_ = old_option_state;
            throw;
        }
        results_ = std::move(old_results);
        default_str_ = std::move(val_str);
        return this;
    }

    /// Get the full typename for this option
    std::string get_type_name() const {
        std::string full_type_name = type_name_();
        if(!validators_.empty()) {
            for(auto &Validator : validators_) {
                std::string vtype = Validator.get_description();
                if(!vtype.empty()) {
                    full_type_name += ":" + vtype;
                }
            }
        }
        return full_type_name;
    }

  private:
    /// Run the results through the Validators
    void _validate_results(results_t &res) const {
        // Run the Validators (can change the string)
        if(!validators_.empty()) {
            if(type_size_max_ > 1) {  // in this context index refers to the index in the type
                int index = 0;
                if(get_items_expected_max() < static_cast<int>(res.size()) &&
                   multi_option_policy_ == CLI::MultiOptionPolicy::TakeLast) {
                    // create a negative index for the earliest ones
                    index = get_items_expected_max() - static_cast<int>(res.size());
                }

                for(std::string &result : res) {
                    if(detail::is_separator(result) && type_size_max_ != type_size_min_ && index >= 0) {
                        index = 0;  // reset index for variable size chunks
                        continue;
                    }
                    auto err_msg = _validate(result, (index >= 0) ? (index % type_size_max_) : index);
                    if(!err_msg.empty())
                        throw ValidationError(get_name(), err_msg);
                    ++index;
                }
            } else {
                int index = 0;
                if(expected_max_ < static_cast<int>(res.size()) &&
                   multi_option_policy_ == CLI::MultiOptionPolicy::TakeLast) {
                    // create a negative index for the earliest ones
                    index = expected_max_ - static_cast<int>(res.size());
                }
                for(std::string &result : res) {
                    auto err_msg = _validate(result, index);
                    ++index;
                    if(!err_msg.empty())
                        throw ValidationError(get_name(), err_msg);
                }
            }
        }
    }

    /** reduce the results in accordance with the MultiOptionPolicy
    @param[out] res results are assigned to res if there if they are different
    */
    void _reduce_results(results_t &res, const results_t &original) const {

        // max num items expected or length of vector, always at least 1
        // Only valid for a trimming policy

        res.clear();
        // Operation depends on the policy setting
        switch(multi_option_policy_) {
        case MultiOptionPolicy::TakeAll:
            break;
        case MultiOptionPolicy::TakeLast: {
            // Allow multi-option sizes (including 0)
            std::size_t trim_size = std::min<std::size_t>(
                static_cast<std::size_t>(std::max<int>(get_items_expected_max(), 1)), original.size());
            if(original.size() != trim_size) {
                res.assign(original.end() - static_cast<results_t::difference_type>(trim_size), original.end());
            }
        } break;
        case MultiOptionPolicy::TakeFirst: {
            std::size_t trim_size = std::min<std::size_t>(
                static_cast<std::size_t>(std::max<int>(get_items_expected_max(), 1)), original.size());
            if(original.size() != trim_size) {
                res.assign(original.begin(), original.begin() + static_cast<results_t::difference_type>(trim_size));
            }
        } break;
        case MultiOptionPolicy::Join:
            if(results_.size() > 1) {
                res.push_back(detail::join(original, std::string(1, (delimiter_ == '\0') ? '\n' : delimiter_)));
            }
            break;
        case MultiOptionPolicy::Sum:
            res.push_back(detail::sum_string_vector(original));
            break;
        case MultiOptionPolicy::Throw:
        default: {
            auto num_min = static_cast<std::size_t>(get_items_expected_min());
            auto num_max = static_cast<std::size_t>(get_items_expected_max());
            if(num_min == 0) {
                num_min = 1;
            }
            if(num_max == 0) {
                num_max = 1;
            }
            if(original.size() < num_min) {
                throw ArgumentMismatch::AtLeast(get_name(), static_cast<int>(num_min), original.size());
            }
            if(original.size() > num_max) {
                throw ArgumentMismatch::AtMost(get_name(), static_cast<int>(num_max), original.size());
            }
            break;
        }
        }
        // this check is to allow an empty vector in certain circumstances but not if expected is not zero.
        // {} is the indicator for a an empty container
        if(res.empty()) {
            if(original.size() == 1 && original[0] == "{}" && get_items_expected_min() > 0) {
                res.push_back("{}");
                res.push_back("%%");
            }
        } else if(res.size() == 1 && res[0] == "{}" && get_items_expected_min() > 0) {
            res.push_back("%%");
        }
    }

    // Run a result through the Validators
    std::string _validate(std::string &result, int index) const {
        std::string err_msg;
        if(result.empty() && expected_min_ == 0) {
            // an empty with nothing expected is allowed
            return err_msg;
        }
        for(const auto &vali : validators_) {
            auto v = vali.get_application_index();
            if(v == -1 || v == index) {
                try {
                    err_msg = vali(result);
                } catch(const ValidationError &err) {
                    err_msg = err.what();
                }
                if(!err_msg.empty())
                    break;
            }
        }

        return err_msg;
    }

    /// Add a single result to the result set, taking into account delimiters
    int _add_result(std::string &&result, std::vector<std::string> &res) const {
        int result_count = 0;
        if(allow_extra_args_ && !result.empty() && result.front() == '[' &&
           result.back() == ']') {  // this is now a vector string likely from the default or user entry
            result.pop_back();

            for(auto &var : CLI::detail::split(result.substr(1), ',')) {
                if(!var.empty()) {
                    result_count += _add_result(std::move(var), res);
                }
            }
            return result_count;
        }
        if(delimiter_ == '\0') {
            res.push_back(std::move(result));
            ++result_count;
        } else {
            if((result.find_first_of(delimiter_) != std::string::npos)) {
                for(const auto &var : CLI::detail::split(result, delimiter_)) {
                    if(!var.empty()) {
                        res.push_back(var);
                        ++result_count;
                    }
                }
            } else {
                res.push_back(std::move(result));
                ++result_count;
            }
        }
        return result_count;
    }
};

// [CLI11:option_hpp:end]
}  // namespace CLI
