// Copyright (c) 2017-2022, University of Cincinnati, developed by Henry Schreiner
// under NSF AWARD 1414736 and by the respective contributors.
// All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// [CLI11:public_includes:set]
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
// [CLI11:public_includes:end]

// CLI Library includes
#include "ConfigFwd.hpp"
#include "Error.hpp"
#include "FormatterFwd.hpp"
#include "Macros.hpp"
#include "Option.hpp"
#include "Split.hpp"
#include "StringTools.hpp"
#include "TypeTools.hpp"

namespace CLI {
// [CLI11:app_hpp:verbatim]

#ifndef CLI11_PARSE
#define CLI11_PARSE(app, argc, argv)                                                                                   \
    try {                                                                                                              \
        (app).parse((argc), (argv));                                                                                   \
    } catch(const CLI::ParseError &e) {                                                                                \
        return (app).exit(e);                                                                                          \
    }
#endif

namespace detail {
enum class Classifier { NONE, POSITIONAL_MARK, SHORT, LONG, WINDOWS_STYLE, SUBCOMMAND, SUBCOMMAND_TERMINATOR };
struct AppFriend;
}  // namespace detail

namespace FailureMessage {
std::string simple(const App *app, const Error &e);
std::string help(const App *app, const Error &e);
}  // namespace FailureMessage

/// enumeration of modes of how to deal with extras in config files

enum class config_extras_mode : char { error = 0, ignore, ignore_all, capture };

class App;

using App_p = std::shared_ptr<App>;

namespace detail {
/// helper functions for adding in appropriate flag modifiers for add_flag

template <typename T, enable_if_t<!std::is_integral<T>::value || (sizeof(T) <= 1U), detail::enabler> = detail::dummy>
Option *default_flag_modifiers(Option *opt) {
    return opt->always_capture_default();
}

/// summing modifiers
template <typename T, enable_if_t<std::is_integral<T>::value && (sizeof(T) > 1U), detail::enabler> = detail::dummy>
Option *default_flag_modifiers(Option *opt) {
    return opt->multi_option_policy(MultiOptionPolicy::Sum)->default_str("0")->force_callback();
}

}  // namespace detail

class Option_group;
/// Creates a command line program, with very few defaults.
/** To use, create a new `Program()` instance with `argc`, `argv`, and a help description. The templated
 *  add_option methods make it easy to prepare options. Remember to call `.start` before starting your
 * program, so that the options can be evaluated and the help option doesn't accidentally run your program. */
class App {
    friend Option;
    friend detail::AppFriend;

  protected:
    // This library follows the Google style guide for member names ending in underscores

    /// @name Basics
    ///@{

    /// Subcommand name or program name (from parser if name is empty)
    std::string name_{};

    /// Description of the current program/subcommand
    std::string description_{};

    /// If true, allow extra arguments (ie, don't throw an error). INHERITABLE
    bool allow_extras_{false};

    /// If ignore, allow extra arguments in the ini file (ie, don't throw an error). INHERITABLE
    /// if error error on an extra argument, and if capture feed it to the app
    config_extras_mode allow_config_extras_{config_extras_mode::ignore};

    ///  If true, return immediately on an unrecognized option (implies allow_extras) INHERITABLE
    bool prefix_command_{false};

    /// If set to true the name was automatically generated from the command line vs a user set name
    bool has_automatic_name_{false};

    /// If set to true the subcommand is required to be processed and used, ignored for main app
    bool required_{false};

    /// If set to true the subcommand is disabled and cannot be used, ignored for main app
    bool disabled_{false};

    /// Flag indicating that the pre_parse_callback has been triggered
    bool pre_parse_called_{false};

    /// Flag indicating that the callback for the subcommand should be executed immediately on parse completion which is
    /// before help or ini files are processed. INHERITABLE
    bool immediate_callback_{false};

    /// This is a function that runs prior to the start of parsing
    std::function<void(std::size_t)> pre_parse_callback_{};

    /// This is a function that runs when parsing has finished.
    std::function<void()> parse_complete_callback_{};

    /// This is a function that runs when all processing has completed
    std::function<void()> final_callback_{};

    ///@}
    /// @name Options
    ///@{

    /// The default values for options, customizable and changeable INHERITABLE
    OptionDefaults option_defaults_{};

    /// The list of options, stored locally
    std::vector<Option_p> options_{};

    ///@}
    /// @name Help
    ///@{

    /// Footer to put after all options in the help output INHERITABLE
    std::string footer_{};

    /// This is a function that generates a footer to put after all other options in help output
    std::function<std::string()> footer_callback_{};

    /// A pointer to the help flag if there is one INHERITABLE
    Option *help_ptr_{nullptr};

    /// A pointer to the help all flag if there is one INHERITABLE
    Option *help_all_ptr_{nullptr};

    /// A pointer to a version flag if there is one
    Option *version_ptr_{nullptr};

    /// This is the formatter for help printing. Default provided. INHERITABLE (same pointer)
    std::shared_ptr<FormatterBase> formatter_{new Formatter()};

    /// The error message printing function INHERITABLE
    std::function<std::string(const App *, const Error &e)> failure_message_{FailureMessage::simple};

    ///@}
    /// @name Parsing
    ///@{

    using missing_t = std::vector<std::pair<detail::Classifier, std::string>>;

    /// Pair of classifier, string for missing options. (extra detail is removed on returning from parse)
    ///
    /// This is faster and cleaner than storing just a list of strings and reparsing. This may contain the -- separator.
    missing_t missing_{};

    /// This is a list of pointers to options with the original parse order
    std::vector<Option *> parse_order_{};

    /// This is a list of the subcommands collected, in order
    std::vector<App *> parsed_subcommands_{};

    /// this is a list of subcommands that are exclusionary to this one
    std::set<App *> exclude_subcommands_{};

    /// This is a list of options which are exclusionary to this App, if the options were used this subcommand should
    /// not be
    std::set<Option *> exclude_options_{};

    /// this is a list of subcommands or option groups that are required by this one, the list is not mutual,  the
    /// listed subcommands do not require this one
    std::set<App *> need_subcommands_{};

    /// This is a list of options which are required by this app, the list is not mutual, listed options do not need the
    /// subcommand not be
    std::set<Option *> need_options_{};

    ///@}
    /// @name Subcommands
    ///@{

    /// Storage for subcommand list
    std::vector<App_p> subcommands_{};

    /// If true, the program name is not case sensitive INHERITABLE
    bool ignore_case_{false};

    /// If true, the program should ignore underscores INHERITABLE
    bool ignore_underscore_{false};

    /// Allow subcommand fallthrough, so that parent commands can collect commands after subcommand.  INHERITABLE
    bool fallthrough_{false};

    /// Allow '/' for options for Windows like options. Defaults to true on Windows, false otherwise. INHERITABLE
    bool allow_windows_style_options_{
#ifdef _WIN32
        true
#else
        false
#endif
    };
    /// specify that positional arguments come at the end of the argument sequence not inheritable
    bool positionals_at_end_{false};

    enum class startup_mode : char { stable, enabled, disabled };
    /// specify the startup mode for the app
    /// stable=no change, enabled= startup enabled, disabled=startup disabled
    startup_mode default_startup{startup_mode::stable};

    /// if set to true the subcommand can be triggered via configuration files INHERITABLE
    bool configurable_{false};

    /// If set to true positional options are validated before assigning INHERITABLE
    bool validate_positionals_{false};

    /// If set to true optional vector arguments are validated before assigning INHERITABLE
    bool validate_optional_arguments_{false};

    /// indicator that the subcommand is silent and won't show up in subcommands list
    /// This is potentially useful as a modifier subcommand
    bool silent_{false};

    /// Counts the number of times this command/subcommand was parsed
    std::uint32_t parsed_{0U};

    /// Minimum required subcommands (not inheritable!)
    std::size_t require_subcommand_min_{0};

    /// Max number of subcommands allowed (parsing stops after this number). 0 is unlimited INHERITABLE
    std::size_t require_subcommand_max_{0};

    /// Minimum required options (not inheritable!)
    std::size_t require_option_min_{0};

    /// Max number of options allowed. 0 is unlimited (not inheritable)
    std::size_t require_option_max_{0};

    /// A pointer to the parent if this is a subcommand
    App *parent_{nullptr};

    /// The group membership INHERITABLE
    std::string group_{"Subcommands"};

    /// Alias names for the subcommand
    std::vector<std::string> aliases_{};

    ///@}
    /// @name Config
    ///@{

    /// Pointer to the config option
    Option *config_ptr_{nullptr};

    /// This is the formatter for help printing. Default provided. INHERITABLE (same pointer)
    std::shared_ptr<Config> config_formatter_{new ConfigTOML()};

    ///@}

    /// Special private constructor for subcommand
    App(std::string app_description, std::string app_name, App *parent)
        : name_(std::move(app_name)), description_(std::move(app_description)), parent_(parent) {
        // Inherit if not from a nullptr
        if(parent_ != nullptr) {
            if(parent_->help_ptr_ != nullptr)
                set_help_flag(parent_->help_ptr_->get_name(false, true), parent_->help_ptr_->get_description());
            if(parent_->help_all_ptr_ != nullptr)
                set_help_all_flag(parent_->help_all_ptr_->get_name(false, true),
                                  parent_->help_all_ptr_->get_description());

            /// OptionDefaults
            option_defaults_ = parent_->option_defaults_;

            // INHERITABLE
            failure_message_ = parent_->failure_message_;
            allow_extras_ = parent_->allow_extras_;
            allow_config_extras_ = parent_->allow_config_extras_;
            prefix_command_ = parent_->prefix_command_;
            immediate_callback_ = parent_->immediate_callback_;
            ignore_case_ = parent_->ignore_case_;
            ignore_underscore_ = parent_->ignore_underscore_;
            fallthrough_ = parent_->fallthrough_;
            validate_positionals_ = parent_->validate_positionals_;
            validate_optional_arguments_ = parent_->validate_optional_arguments_;
            configurable_ = parent_->configurable_;
            allow_windows_style_options_ = parent_->allow_windows_style_options_;
            group_ = parent_->group_;
            footer_ = parent_->footer_;
            formatter_ = parent_->formatter_;
            config_formatter_ = parent_->config_formatter_;
            require_subcommand_max_ = parent_->require_subcommand_max_;
        }
    }

  public:
    /// @name Basic
    ///@{

    /// Create a new program. Pass in the same arguments as main(), along with a help string.
    explicit App(std::string app_description = "", std::string app_name = "")
        : App(app_description, app_name, nullptr) {
        set_help_flag("-h,--help", "Print this help message and exit");
    }

    App(const App &) = delete;
    App &operator=(const App &) = delete;

    /// virtual destructor
    virtual ~App() = default;

    /// Set a callback for execution when all parsing and processing has completed
    ///
    /// Due to a bug in c++11,
    /// it is not possible to overload on std::function (fixed in c++14
    /// and backported to c++11 on newer compilers). Use capture by reference
    /// to get a pointer to App if needed.
    App *callback(std::function<void()> app_callback) {
        if(immediate_callback_) {
            parse_complete_callback_ = std::move(app_callback);
        } else {
            final_callback_ = std::move(app_callback);
        }
        return this;
    }

    /// Set a callback for execution when all parsing and processing has completed
    /// aliased as callback
    App *final_callback(std::function<void()> app_callback) {
        final_callback_ = std::move(app_callback);
        return this;
    }

    /// Set a callback to execute when parsing has completed for the app
    ///
    App *parse_complete_callback(std::function<void()> pc_callback) {
        parse_complete_callback_ = std::move(pc_callback);
        return this;
    }

    /// Set a callback to execute prior to parsing.
    ///
    App *preparse_callback(std::function<void(std::size_t)> pp_callback) {
        pre_parse_callback_ = std::move(pp_callback);
        return this;
    }

    /// Set a name for the app (empty will use parser to set the name)
    App *name(std::string app_name = "") {

        if(parent_ != nullptr) {
            auto oname = name_;
            name_ = app_name;
            auto &res = _compare_subcommand_names(*this, *_get_fallthrough_parent());
            if(!res.empty()) {
                name_ = oname;
                throw(OptionAlreadyAdded(app_name + " conflicts with existing subcommand names"));
            }
        } else {
            name_ = app_name;
        }
        has_automatic_name_ = false;
        return this;
    }

    /// Set an alias for the app
    App *alias(std::string app_name) {
        if(app_name.empty() || !detail::valid_alias_name_string(app_name)) {
            throw IncorrectConstruction("Aliases may not be empty or contain newlines or null characters");
        }
        if(parent_ != nullptr) {
            aliases_.push_back(app_name);
            auto &res = _compare_subcommand_names(*this, *_get_fallthrough_parent());
            if(!res.empty()) {
                aliases_.pop_back();
                throw(OptionAlreadyAdded("alias already matches an existing subcommand: " + app_name));
            }
        } else {
            aliases_.push_back(app_name);
        }

        return this;
    }

    /// Remove the error when extras are left over on the command line.
    App *allow_extras(bool allow = true) {
        allow_extras_ = allow;
        return this;
    }

    /// Remove the error when extras are left over on the command line.
    App *required(bool require = true) {
        required_ = require;
        return this;
    }

    /// Disable the subcommand or option group
    App *disabled(bool disable = true) {
        disabled_ = disable;
        return this;
    }

    /// silence the subcommand from showing up in the processed list
    App *silent(bool silence = true) {
        silent_ = silence;
        return this;
    }

    /// Set the subcommand to be disabled by default, so on clear(), at the start of each parse it is disabled
    App *disabled_by_default(bool disable = true) {
        if(disable) {
            default_startup = startup_mode::disabled;
        } else {
            default_startup = (default_startup == startup_mode::enabled) ? startup_mode::enabled : startup_mode::stable;
        }
        return this;
    }

    /// Set the subcommand to be enabled by default, so on clear(), at the start of each parse it is enabled (not
    /// disabled)
    App *enabled_by_default(bool enable = true) {
        if(enable) {
            default_startup = startup_mode::enabled;
        } else {
            default_startup =
                (default_startup == startup_mode::disabled) ? startup_mode::disabled : startup_mode::stable;
        }
        return this;
    }

    /// Set the subcommand callback to be executed immediately on subcommand completion
    App *immediate_callback(bool immediate = true) {
        immediate_callback_ = immediate;
        if(immediate_callback_) {
            if(final_callback_ && !(parse_complete_callback_)) {
                std::swap(final_callback_, parse_complete_callback_);
            }
        } else if(!(final_callback_) && parse_complete_callback_) {
            std::swap(final_callback_, parse_complete_callback_);
        }
        return this;
    }

    /// Set the subcommand to validate positional arguments before assigning
    App *validate_positionals(bool validate = true) {
        validate_positionals_ = validate;
        return this;
    }

    /// Set the subcommand to validate optional vector arguments before assigning
    App *validate_optional_arguments(bool validate = true) {
        validate_optional_arguments_ = validate;
        return this;
    }

    /// ignore extras in config files
    App *allow_config_extras(bool allow = true) {
        if(allow) {
            allow_config_extras_ = config_extras_mode::capture;
            allow_extras_ = true;
        } else {
            allow_config_extras_ = config_extras_mode::error;
        }
        return this;
    }

    /// ignore extras in config files
    App *allow_config_extras(config_extras_mode mode) {
        allow_config_extras_ = mode;
        return this;
    }

    /// Do not parse anything after the first unrecognized option and return
    App *prefix_command(bool allow = true) {
        prefix_command_ = allow;
        return this;
    }

    /// Ignore case. Subcommands inherit value.
    App *ignore_case(bool value = true) {
        if(value && !ignore_case_) {
            ignore_case_ = true;
            auto *p = (parent_ != nullptr) ? _get_fallthrough_parent() : this;
            auto &match = _compare_subcommand_names(*this, *p);
            if(!match.empty()) {
                ignore_case_ = false;  // we are throwing so need to be exception invariant
                throw OptionAlreadyAdded("ignore case would cause subcommand name conflicts: " + match);
            }
        }
        ignore_case_ = value;
        return this;
    }

    /// Allow windows style options, such as `/opt`. First matching short or long name used. Subcommands inherit
    /// value.
    App *allow_windows_style_options(bool value = true) {
        allow_windows_style_options_ = value;
        return this;
    }

    /// Specify that the positional arguments are only at the end of the sequence
    App *positionals_at_end(bool value = true) {
        positionals_at_end_ = value;
        return this;
    }

    /// Specify that the subcommand can be triggered by a config file
    App *configurable(bool value = true) {
        configurable_ = value;
        return this;
    }

    /// Ignore underscore. Subcommands inherit value.
    App *ignore_underscore(bool value = true) {
        if(value && !ignore_underscore_) {
            ignore_underscore_ = true;
            auto *p = (parent_ != nullptr) ? _get_fallthrough_parent() : this;
            auto &match = _compare_subcommand_names(*this, *p);
            if(!match.empty()) {
                ignore_underscore_ = false;
                throw OptionAlreadyAdded("ignore underscore would cause subcommand name conflicts: " + match);
            }
        }
        ignore_underscore_ = value;
        return this;
    }

    /// Set the help formatter
    App *formatter(std::shared_ptr<FormatterBase> fmt) {
        formatter_ = fmt;
        return this;
    }

    /// Set the help formatter
    App *formatter_fn(std::function<std::string(const App *, std::string, AppFormatMode)> fmt) {
        formatter_ = std::make_shared<FormatterLambda>(fmt);
        return this;
    }

    /// Set the config formatter
    App *config_formatter(std::shared_ptr<Config> fmt) {
        config_formatter_ = fmt;
        return this;
    }

    /// Check to see if this subcommand was parsed, true only if received on command line.
    bool parsed() const { return parsed_ > 0; }

    /// Get the OptionDefault object, to set option defaults
    OptionDefaults *option_defaults() { return &option_defaults_; }

    ///@}
    /// @name Adding options
    ///@{

    /// Add an option, will automatically understand the type for common types.
    ///
    /// To use, create a variable with the expected type, and pass it in after the name.
    /// After start is called, you can use count to see if the value was passed, and
    /// the value will be initialized properly. Numbers, vectors, and strings are supported.
    ///
    /// ->required(), ->default, and the validators are options,
    /// The positional options take an optional number of arguments.
    ///
    /// For example,
    ///
    ///     std::string filename;
    ///     program.add_option("filename", filename, "description of filename");
    ///
    Option *add_option(std::string option_name,
                       callback_t option_callback,
                       std::string option_description = "",
                       bool defaulted = false,
                       std::function<std::string()> func = {}) {
        Option myopt{option_name, option_description, option_callback, this};

        if(std::find_if(std::begin(options_), std::end(options_), [&myopt](const Option_p &v) {
               return *v == myopt;
           }) == std::end(options_)) {
            options_.emplace_back();
            Option_p &option = options_.back();
            option.reset(new Option(option_name, option_description, option_callback, this));

            // Set the default string capture function
            option->default_function(func);

            // For compatibility with CLI11 1.7 and before, capture the default string here
            if(defaulted)
                option->capture_default_str();

            // Transfer defaults to the new option
            option_defaults_.copy_to(option.get());

            // Don't bother to capture if we already did
            if(!defaulted && option->get_always_capture_default())
                option->capture_default_str();

            return option.get();
        }
        // we know something matches now find what it is so we can produce more error information
        for(auto &opt : options_) {
            auto &matchname = opt->matching_name(myopt);
            if(!matchname.empty()) {
                throw(OptionAlreadyAdded("added option matched existing option name: " + matchname));
            }
        }
        // this line should not be reached the above loop should trigger the throw
        throw(OptionAlreadyAdded("added option matched existing option name"));  // LCOV_EXCL_LINE
    }

    /// Add option for assigning to a variable
    template <typename AssignTo,
              typename ConvertTo = AssignTo,
              enable_if_t<!std::is_const<ConvertTo>::value, detail::enabler> = detail::dummy>
    Option *add_option(std::string option_name,
                       AssignTo &variable,  ///< The variable to set
                       std::string option_description = "") {

        auto fun = [&variable](const CLI::results_t &res) {  // comment for spacing
            return detail::lexical_conversion<AssignTo, ConvertTo>(res, variable);
        };

        Option *opt = add_option(option_name, fun, option_description, false, [&variable]() {
            return CLI::detail::checked_to_string<AssignTo, ConvertTo>(variable);
        });
        opt->type_name(detail::type_name<ConvertTo>());
        // these must be actual lvalues since (std::max) sometimes is defined in terms of references and references
        // to structs used in the evaluation can be temporary so that would cause issues.
        auto Tcount = detail::type_count<AssignTo>::value;
        auto XCcount = detail::type_count<ConvertTo>::value;
        opt->type_size(detail::type_count_min<ConvertTo>::value, (std::max)(Tcount, XCcount));
        opt->expected(detail::expected_count<ConvertTo>::value);
        opt->run_callback_for_default();
        return opt;
    }

    /// Add option for assigning to a variable
    template <typename AssignTo, enable_if_t<!std::is_const<AssignTo>::value, detail::enabler> = detail::dummy>
    Option *add_option_no_stream(std::string option_name,
                                 AssignTo &variable,  ///< The variable to set
                                 std::string option_description = "") {

        auto fun = [&variable](const CLI::results_t &res) {  // comment for spacing
            return detail::lexical_conversion<AssignTo, AssignTo>(res, variable);
        };

        Option *opt = add_option(option_name, fun, option_description, false, []() { return std::string{}; });
        opt->type_name(detail::type_name<AssignTo>());
        opt->type_size(detail::type_count_min<AssignTo>::value, detail::type_count<AssignTo>::value);
        opt->expected(detail::expected_count<AssignTo>::value);
        opt->run_callback_for_default();
        return opt;
    }

    /// Add option for a callback of a specific type
    template <typename ArgType>
    Option *add_option_function(std::string option_name,
                                const std::function<void(const ArgType &)> &func,  ///< the callback to execute
                                std::string option_description = "") {

        auto fun = [func](const CLI::results_t &res) {
            ArgType variable;
            bool result = detail::lexical_conversion<ArgType, ArgType>(res, variable);
            if(result) {
                func(variable);
            }
            return result;
        };

        Option *opt = add_option(option_name, std::move(fun), option_description, false);
        opt->type_name(detail::type_name<ArgType>());
        opt->type_size(detail::type_count_min<ArgType>::value, detail::type_count<ArgType>::value);
        opt->expected(detail::expected_count<ArgType>::value);
        return opt;
    }

    /// Add option with no description or variable assignment
    Option *add_option(std::string option_name) {
        return add_option(option_name, CLI::callback_t{}, std::string{}, false);
    }

    /// Add option with description but with no variable assignment or callback
    template <typename T,
              enable_if_t<std::is_const<T>::value && std::is_constructible<std::string, T>::value, detail::enabler> =
                  detail::dummy>
    Option *add_option(std::string option_name, T &option_description) {
        return add_option(option_name, CLI::callback_t(), option_description, false);
    }

    /// Set a help flag, replace the existing one if present
    Option *set_help_flag(std::string flag_name = "", const std::string &help_description = "") {
        // take flag_description by const reference otherwise add_flag tries to assign to help_description
        if(help_ptr_ != nullptr) {
            remove_option(help_ptr_);
            help_ptr_ = nullptr;
        }

        // Empty name will simply remove the help flag
        if(!flag_name.empty()) {
            help_ptr_ = add_flag(flag_name, help_description);
            help_ptr_->configurable(false);
        }

        return help_ptr_;
    }

    /// Set a help all flag, replaced the existing one if present
    Option *set_help_all_flag(std::string help_name = "", const std::string &help_description = "") {
        // take flag_description by const reference otherwise add_flag tries to assign to flag_description
        if(help_all_ptr_ != nullptr) {
            remove_option(help_all_ptr_);
            help_all_ptr_ = nullptr;
        }

        // Empty name will simply remove the help all flag
        if(!help_name.empty()) {
            help_all_ptr_ = add_flag(help_name, help_description);
            help_all_ptr_->configurable(false);
        }

        return help_all_ptr_;
    }

    /// Set a version flag and version display string, replace the existing one if present
    Option *set_version_flag(std::string flag_name = "",
                             const std::string &versionString = "",
                             const std::string &version_help = "Display program version information and exit") {
        // take flag_description by const reference otherwise add_flag tries to assign to version_description
        if(version_ptr_ != nullptr) {
            remove_option(version_ptr_);
            version_ptr_ = nullptr;
        }

        // Empty name will simply remove the version flag
        if(!flag_name.empty()) {
            version_ptr_ = add_flag_callback(
                flag_name, [versionString]() { throw(CLI::CallForVersion(versionString, 0)); }, version_help);
            version_ptr_->configurable(false);
        }

        return version_ptr_;
    }
    /// Generate the version string through a callback function
    Option *set_version_flag(std::string flag_name,
                             std::function<std::string()> vfunc,
                             const std::string &version_help = "Display program version information and exit") {
        if(version_ptr_ != nullptr) {
            remove_option(version_ptr_);
            version_ptr_ = nullptr;
        }

        // Empty name will simply remove the version flag
        if(!flag_name.empty()) {
            version_ptr_ = add_flag_callback(
                flag_name, [vfunc]() { throw(CLI::CallForVersion(vfunc(), 0)); }, version_help);
            version_ptr_->configurable(false);
        }

        return version_ptr_;
    }

  private:
    /// Internal function for adding a flag
    Option *_add_flag_internal(std::string flag_name, CLI::callback_t fun, std::string flag_description) {
        Option *opt;
        if(detail::has_default_flag_values(flag_name)) {
            // check for default values and if it has them
            auto flag_defaults = detail::get_default_flag_values(flag_name);
            detail::remove_default_flag_values(flag_name);
            opt = add_option(std::move(flag_name), std::move(fun), std::move(flag_description), false);
            for(const auto &fname : flag_defaults)
                opt->fnames_.push_back(fname.first);
            opt->default_flag_values_ = std::move(flag_defaults);
        } else {
            opt = add_option(std::move(flag_name), std::move(fun), std::move(flag_description), false);
        }
        // flags cannot have positional values
        if(opt->get_positional()) {
            auto pos_name = opt->get_name(true);
            remove_option(opt);
            throw IncorrectConstruction::PositionalFlag(pos_name);
        }
        opt->multi_option_policy(MultiOptionPolicy::TakeLast);
        opt->expected(0);
        opt->required(false);
        return opt;
    }

  public:
    /// Add a flag with no description or variable assignment
    Option *add_flag(std::string flag_name) { return _add_flag_internal(flag_name, CLI::callback_t(), std::string{}); }

    /// Add flag with description but with no variable assignment or callback
    /// takes a constant string,  if a variable string is passed that variable will be assigned the results from the
    /// flag
    template <typename T,
              enable_if_t<std::is_const<T>::value && std::is_constructible<std::string, T>::value, detail::enabler> =
                  detail::dummy>
    Option *add_flag(std::string flag_name, T &flag_description) {
        return _add_flag_internal(flag_name, CLI::callback_t(), flag_description);
    }

    /// Other type version accepts all other types that are not vectors such as bool, enum, string or other classes
    /// that can be converted from a string
    template <typename T,
              enable_if_t<!detail::is_mutable_container<T>::value && !std::is_const<T>::value &&
                              !std::is_constructible<std::function<void(int)>, T>::value,
                          detail::enabler> = detail::dummy>
    Option *add_flag(std::string flag_name,
                     T &flag_result,  ///< A variable holding the flag result
                     std::string flag_description = "") {

        CLI::callback_t fun = [&flag_result](const CLI::results_t &res) {
            return CLI::detail::lexical_cast(res[0], flag_result);
        };
        auto *opt = _add_flag_internal(flag_name, std::move(fun), std::move(flag_description));
        return detail::default_flag_modifiers<T>(opt);
    }

    /// Vector version to capture multiple flags.
    template <typename T,
              enable_if_t<!std::is_assignable<std::function<void(std::int64_t)> &, T>::value, detail::enabler> =
                  detail::dummy>
    Option *add_flag(std::string flag_name,
                     std::vector<T> &flag_results,  ///< A vector of values with the flag results
                     std::string flag_description = "") {
        CLI::callback_t fun = [&flag_results](const CLI::results_t &res) {
            bool retval = true;
            for(const auto &elem : res) {
                flag_results.emplace_back();
                retval &= detail::lexical_cast(elem, flag_results.back());
            }
            return retval;
        };
        return _add_flag_internal(flag_name, std::move(fun), std::move(flag_description))
            ->multi_option_policy(MultiOptionPolicy::TakeAll)
            ->run_callback_for_default();
    }

    /// Add option for callback that is triggered with a true flag and takes no arguments
    Option *add_flag_callback(std::string flag_name,
                              std::function<void(void)> function,  ///< A function to call, void(void)
                              std::string flag_description = "") {

        CLI::callback_t fun = [function](const CLI::results_t &res) {
            bool trigger{false};
            auto result = CLI::detail::lexical_cast(res[0], trigger);
            if(result && trigger) {
                function();
            }
            return result;
        };
        return _add_flag_internal(flag_name, std::move(fun), std::move(flag_description));
    }

    /// Add option for callback with an integer value
    Option *add_flag_function(std::string flag_name,
                              std::function<void(std::int64_t)> function,  ///< A function to call, void(int)
                              std::string flag_description = "") {

        CLI::callback_t fun = [function](const CLI::results_t &res) {
            std::int64_t flag_count{0};
            CLI::detail::lexical_cast(res[0], flag_count);
            function(flag_count);
            return true;
        };
        return _add_flag_internal(flag_name, std::move(fun), std::move(flag_description))
            ->multi_option_policy(MultiOptionPolicy::Sum);
    }

#ifdef CLI11_CPP14
    /// Add option for callback (C++14 or better only)
    Option *add_flag(std::string flag_name,
                     std::function<void(std::int64_t)> function,  ///< A function to call, void(std::int64_t)
                     std::string flag_description = "") {
        return add_flag_function(std::move(flag_name), std::move(function), std::move(flag_description));
    }
#endif

    /// Set a configuration ini file option, or clear it if no name passed
    Option *set_config(std::string option_name = "",
                       std::string default_filename = "",
                       const std::string &help_message = "Read an ini file",
                       bool config_required = false) {

        // Remove existing config if present
        if(config_ptr_ != nullptr) {
            remove_option(config_ptr_);
            config_ptr_ = nullptr;  // need to remove the config_ptr completely
        }

        // Only add config if option passed
        if(!option_name.empty()) {
            config_ptr_ = add_option(option_name, help_message);
            if(config_required) {
                config_ptr_->required();
            }
            if(!default_filename.empty()) {
                config_ptr_->default_str(std::move(default_filename));
            }
            config_ptr_->configurable(false);
        }

        return config_ptr_;
    }

    /// Removes an option from the App. Takes an option pointer. Returns true if found and removed.
    bool remove_option(Option *opt) {
        // Make sure no links exist
        for(Option_p &op : options_) {
            op->remove_needs(opt);
            op->remove_excludes(opt);
        }

        if(help_ptr_ == opt)
            help_ptr_ = nullptr;
        if(help_all_ptr_ == opt)
            help_all_ptr_ = nullptr;

        auto iterator =
            std::find_if(std::begin(options_), std::end(options_), [opt](const Option_p &v) { return v.get() == opt; });
        if(iterator != std::end(options_)) {
            options_.erase(iterator);
            return true;
        }
        return false;
    }

    /// creates an option group as part of the given app
    template <typename T = Option_group>
    T *add_option_group(std::string group_name, std::string group_description = "") {
        if(!detail::valid_alias_name_string(group_name)) {
            throw IncorrectConstruction("option group names may not contain newlines or null characters");
        }
        auto option_group = std::make_shared<T>(std::move(group_description), group_name, this);
        auto ptr = option_group.get();
        // move to App_p for overload resolution on older gcc versions
        App_p app_ptr = std::dynamic_pointer_cast<App>(option_group);
        add_subcommand(std::move(app_ptr));
        return ptr;
    }

    ///@}
    /// @name Subcommands
    ///@{

    /// Add a subcommand. Inherits INHERITABLE and OptionDefaults, and help flag
    App *add_subcommand(std::string subcommand_name = "", std::string subcommand_description = "") {
        if(!subcommand_name.empty() && !detail::valid_name_string(subcommand_name)) {
            if(!detail::valid_first_char(subcommand_name[0])) {
                throw IncorrectConstruction(
                    "Subcommand name starts with invalid character, '!' and '-' are not allowed");
            }
            for(auto c : subcommand_name) {
                if(!detail::valid_later_char(c)) {
                    throw IncorrectConstruction(std::string("Subcommand name contains invalid character ('") + c +
                                                "'), all characters are allowed except"
                                                "'=',':','{','}', and ' '");
                }
            }
        }
        CLI::App_p subcom = std::shared_ptr<App>(new App(std::move(subcommand_description), subcommand_name, this));
        return add_subcommand(std::move(subcom));
    }

    /// Add a previously created app as a subcommand
    App *add_subcommand(CLI::App_p subcom) {
        if(!subcom)
            throw IncorrectConstruction("passed App is not valid");
        auto ckapp = (name_.empty() && parent_ != nullptr) ? _get_fallthrough_parent() : this;
        auto &mstrg = _compare_subcommand_names(*subcom, *ckapp);
        if(!mstrg.empty()) {
            throw(OptionAlreadyAdded("subcommand name or alias matches existing subcommand: " + mstrg));
        }
        subcom->parent_ = this;
        subcommands_.push_back(std::move(subcom));
        return subcommands_.back().get();
    }

    /// Removes a subcommand from the App. Takes a subcommand pointer. Returns true if found and removed.
    bool remove_subcommand(App *subcom) {
        // Make sure no links exist
        for(App_p &sub : subcommands_) {
            sub->remove_excludes(subcom);
            sub->remove_needs(subcom);
        }

        auto iterator = std::find_if(
            std::begin(subcommands_), std::end(subcommands_), [subcom](const App_p &v) { return v.get() == subcom; });
        if(iterator != std::end(subcommands_)) {
            subcommands_.erase(iterator);
            return true;
        }
        return false;
    }
    /// Check to see if a subcommand is part of this command (doesn't have to be in command line)
    /// returns the first subcommand if passed a nullptr
    App *get_subcommand(const App *subcom) const {
        if(subcom == nullptr)
            throw OptionNotFound("nullptr passed");
        for(const App_p &subcomptr : subcommands_)
            if(subcomptr.get() == subcom)
                return subcomptr.get();
        throw OptionNotFound(subcom->get_name());
    }

    /// Check to see if a subcommand is part of this command (text version)
    App *get_subcommand(std::string subcom) const {
        auto subc = _find_subcommand(subcom, false, false);
        if(subc == nullptr)
            throw OptionNotFound(subcom);
        return subc;
    }
    /// Get a pointer to subcommand by index
    App *get_subcommand(int index = 0) const {
        if(index >= 0) {
            auto uindex = static_cast<unsigned>(index);
            if(uindex < subcommands_.size())
                return subcommands_[uindex].get();
        }
        throw OptionNotFound(std::to_string(index));
    }

    /// Check to see if a subcommand is part of this command and get a shared_ptr to it
    CLI::App_p get_subcommand_ptr(App *subcom) const {
        if(subcom == nullptr)
            throw OptionNotFound("nullptr passed");
        for(const App_p &subcomptr : subcommands_)
            if(subcomptr.get() == subcom)
                return subcomptr;
        throw OptionNotFound(subcom->get_name());
    }

    /// Check to see if a subcommand is part of this command (text version)
    CLI::App_p get_subcommand_ptr(std::string subcom) const {
        for(const App_p &subcomptr : subcommands_)
            if(subcomptr->check_name(subcom))
                return subcomptr;
        throw OptionNotFound(subcom);
    }

    /// Get an owning pointer to subcommand by index
    CLI::App_p get_subcommand_ptr(int index = 0) const {
        if(index >= 0) {
            auto uindex = static_cast<unsigned>(index);
            if(uindex < subcommands_.size())
                return subcommands_[uindex];
        }
        throw OptionNotFound(std::to_string(index));
    }

    /// Check to see if an option group is part of this App
    App *get_option_group(std::string group_name) const {
        for(const App_p &app : subcommands_) {
            if(app->name_.empty() && app->group_ == group_name) {
                return app.get();
            }
        }
        throw OptionNotFound(group_name);
    }

    /// No argument version of count counts the number of times this subcommand was
    /// passed in. The main app will return 1. Unnamed subcommands will also return 1 unless
    /// otherwise modified in a callback
    std::size_t count() const { return parsed_; }

    /// Get a count of all the arguments processed in options and subcommands, this excludes arguments which were
    /// treated as extras.
    std::size_t count_all() const {
        std::size_t cnt{0};
        for(auto &opt : options_) {
            cnt += opt->count();
        }
        for(auto &sub : subcommands_) {
            cnt += sub->count_all();
        }
        if(!get_name().empty()) {  // for named subcommands add the number of times the subcommand was called
            cnt += parsed_;
        }
        return cnt;
    }

    /// Changes the group membership
    App *group(std::string group_name) {
        group_ = group_name;
        return this;
    }

    /// The argumentless form of require subcommand requires 1 or more subcommands
    App *require_subcommand() {
        require_subcommand_min_ = 1;
        require_subcommand_max_ = 0;
        return this;
    }

    /// Require a subcommand to be given (does not affect help call)
    /// The number required can be given. Negative values indicate maximum
    /// number allowed (0 for any number). Max number inheritable.
    App *require_subcommand(int value) {
        if(value < 0) {
            require_subcommand_min_ = 0;
            require_subcommand_max_ = static_cast<std::size_t>(-value);
        } else {
            require_subcommand_min_ = static_cast<std::size_t>(value);
            require_subcommand_max_ = static_cast<std::size_t>(value);
        }
        return this;
    }

    /// Explicitly control the number of subcommands required. Setting 0
    /// for the max means unlimited number allowed. Max number inheritable.
    App *require_subcommand(std::size_t min, std::size_t max) {
        require_subcommand_min_ = min;
        require_subcommand_max_ = max;
        return this;
    }

    /// The argumentless form of require option requires 1 or more options be used
    App *require_option() {
        require_option_min_ = 1;
        require_option_max_ = 0;
        return this;
    }

    /// Require an option to be given (does not affect help call)
    /// The number required can be given. Negative values indicate maximum
    /// number allowed (0 for any number).
    App *require_option(int value) {
        if(value < 0) {
            require_option_min_ = 0;
            require_option_max_ = static_cast<std::size_t>(-value);
        } else {
            require_option_min_ = static_cast<std::size_t>(value);
            require_option_max_ = static_cast<std::size_t>(value);
        }
        return this;
    }

    /// Explicitly control the number of options required. Setting 0
    /// for the max means unlimited number allowed. Max number inheritable.
    App *require_option(std::size_t min, std::size_t max) {
        require_option_min_ = min;
        require_option_max_ = max;
        return this;
    }

    /// Stop subcommand fallthrough, so that parent commands cannot collect commands after subcommand.
    /// Default from parent, usually set on parent.
    App *fallthrough(bool value = true) {
        fallthrough_ = value;
        return this;
    }

    /// Check to see if this subcommand was parsed, true only if received on command line.
    /// This allows the subcommand to be directly checked.
    explicit operator bool() const { return parsed_ > 0; }

    ///@}
    /// @name Extras for subclassing
    ///@{

    /// This allows subclasses to inject code before callbacks but after parse.
    ///
    /// This does not run if any errors or help is thrown.
    virtual void pre_callback() {}

    ///@}
    /// @name Parsing
    ///@{
    //
    /// Reset the parsed data
    void clear() {

        parsed_ = 0;
        pre_parse_called_ = false;

        missing_.clear();
        parsed_subcommands_.clear();
        for(const Option_p &opt : options_) {
            opt->clear();
        }
        for(const App_p &subc : subcommands_) {
            subc->clear();
        }
    }

    /// Parses the command line - throws errors.
    /// This must be called after the options are in but before the rest of the program.
    void parse(int argc, const char *const *argv) {
        // If the name is not set, read from command line
        if(name_.empty() || has_automatic_name_) {
            has_automatic_name_ = true;
            name_ = argv[0];
        }

        std::vector<std::string> args;
        args.reserve(static_cast<std::size_t>(argc) - 1U);
        for(auto i = static_cast<std::size_t>(argc) - 1U; i > 0U; --i)
            args.emplace_back(argv[i]);
        parse(std::move(args));
    }

    /// Parse a single string as if it contained command line arguments.
    /// This function splits the string into arguments then calls parse(std::vector<std::string> &)
    /// the function takes an optional boolean argument specifying if the programName is included in the string to
    /// process
    void parse(std::string commandline, bool program_name_included = false) {

        if(program_name_included) {
            auto nstr = detail::split_program_name(commandline);
            if((name_.empty()) || (has_automatic_name_)) {
                has_automatic_name_ = true;
                name_ = nstr.first;
            }
            commandline = std::move(nstr.second);
        } else {
            detail::trim(commandline);
        }
        // the next section of code is to deal with quoted arguments after an '=' or ':' for windows like operations
        if(!commandline.empty()) {
            commandline = detail::find_and_modify(commandline, "=", detail::escape_detect);
            if(allow_windows_style_options_)
                commandline = detail::find_and_modify(commandline, ":", detail::escape_detect);
        }

        auto args = detail::split_up(std::move(commandline));
        // remove all empty strings
        args.erase(std::remove(args.begin(), args.end(), std::string{}), args.end());
        std::reverse(args.begin(), args.end());

        parse(std::move(args));
    }

    /// The real work is done here. Expects a reversed vector.
    /// Changes the vector to the remaining options.
    void parse(std::vector<std::string> &args) {
        // Clear if parsed
        if(parsed_ > 0)
            clear();

        // parsed_ is incremented in commands/subcommands,
        // but placed here to make sure this is cleared when
        // running parse after an error is thrown, even by _validate or _configure.
        parsed_ = 1;
        _validate();
        _configure();
        // set the parent as nullptr as this object should be the top now
        parent_ = nullptr;
        parsed_ = 0;

        _parse(args);
        run_callback();
    }

    /// The real work is done here. Expects a reversed vector.
    void parse(std::vector<std::string> &&args) {
        // Clear if parsed
        if(parsed_ > 0)
            clear();

        // parsed_ is incremented in commands/subcommands,
        // but placed here to make sure this is cleared when
        // running parse after an error is thrown, even by _validate or _configure.
        parsed_ = 1;
        _validate();
        _configure();
        // set the parent as nullptr as this object should be the top now
        parent_ = nullptr;
        parsed_ = 0;

        _parse(std::move(args));
        run_callback();
    }

    void parse_from_stream(std::istream &input) {
        if(parsed_ == 0) {
            _validate();
            _configure();
            // set the parent as nullptr as this object should be the top now
        }

        _parse_stream(input);
        run_callback();
    }
    /// Provide a function to print a help message. The function gets access to the App pointer and error.
    void failure_message(std::function<std::string(const App *, const Error &e)> function) {
        failure_message_ = function;
    }

    /// Print a nice error message and return the exit code
    int exit(const Error &e, std::ostream &out = std::cout, std::ostream &err = std::cerr) const {

        /// Avoid printing anything if this is a CLI::RuntimeError
        if(e.get_name() == "RuntimeError")
            return e.get_exit_code();

        if(e.get_name() == "CallForHelp") {
            out << help();
            return e.get_exit_code();
        }

        if(e.get_name() == "CallForAllHelp") {
            out << help("", AppFormatMode::All);
            return e.get_exit_code();
        }

        if(e.get_name() == "CallForVersion") {
            out << e.what() << std::endl;
            return e.get_exit_code();
        }

        if(e.get_exit_code() != static_cast<int>(ExitCodes::Success)) {
            if(failure_message_)
                err << failure_message_(this, e) << std::flush;
        }

        return e.get_exit_code();
    }

    ///@}
    /// @name Post parsing
    ///@{

    /// Counts the number of times the given option was passed.
    std::size_t count(std::string option_name) const { return get_option(option_name)->count(); }

    /// Get a subcommand pointer list to the currently selected subcommands (after parsing by default, in command
    /// line order; use parsed = false to get the original definition list.)
    std::vector<App *> get_subcommands() const { return parsed_subcommands_; }

    /// Get a filtered subcommand pointer list from the original definition list. An empty function will provide all
    /// subcommands (const)
    std::vector<const App *> get_subcommands(const std::function<bool(const App *)> &filter) const {
        std::vector<const App *> subcomms(subcommands_.size());
        std::transform(std::begin(subcommands_), std::end(subcommands_), std::begin(subcomms), [](const App_p &v) {
            return v.get();
        });

        if(filter) {
            subcomms.erase(std::remove_if(std::begin(subcomms),
                                          std::end(subcomms),
                                          [&filter](const App *app) { return !filter(app); }),
                           std::end(subcomms));
        }

        return subcomms;
    }

    /// Get a filtered subcommand pointer list from the original definition list. An empty function will provide all
    /// subcommands
    std::vector<App *> get_subcommands(const std::function<bool(App *)> &filter) {
        std::vector<App *> subcomms(subcommands_.size());
        std::transform(std::begin(subcommands_), std::end(subcommands_), std::begin(subcomms), [](const App_p &v) {
            return v.get();
        });

        if(filter) {
            subcomms.erase(
                std::remove_if(std::begin(subcomms), std::end(subcomms), [&filter](App *app) { return !filter(app); }),
                std::end(subcomms));
        }

        return subcomms;
    }

    /// Check to see if given subcommand was selected
    bool got_subcommand(const App *subcom) const {
        // get subcom needed to verify that this was a real subcommand
        return get_subcommand(subcom)->parsed_ > 0;
    }

    /// Check with name instead of pointer to see if subcommand was selected
    bool got_subcommand(std::string subcommand_name) const { return get_subcommand(subcommand_name)->parsed_ > 0; }

    /// Sets excluded options for the subcommand
    App *excludes(Option *opt) {
        if(opt == nullptr) {
            throw OptionNotFound("nullptr passed");
        }
        exclude_options_.insert(opt);
        return this;
    }

    /// Sets excluded subcommands for the subcommand
    App *excludes(App *app) {
        if(app == nullptr) {
            throw OptionNotFound("nullptr passed");
        }
        if(app == this) {
            throw OptionNotFound("cannot self reference in needs");
        }
        auto res = exclude_subcommands_.insert(app);
        // subcommand exclusion should be symmetric
        if(res.second) {
            app->exclude_subcommands_.insert(this);
        }
        return this;
    }

    App *needs(Option *opt) {
        if(opt == nullptr) {
            throw OptionNotFound("nullptr passed");
        }
        need_options_.insert(opt);
        return this;
    }

    App *needs(App *app) {
        if(app == nullptr) {
            throw OptionNotFound("nullptr passed");
        }
        if(app == this) {
            throw OptionNotFound("cannot self reference in needs");
        }
        need_subcommands_.insert(app);
        return this;
    }

    /// Removes an option from the excludes list of this subcommand
    bool remove_excludes(Option *opt) {
        auto iterator = std::find(std::begin(exclude_options_), std::end(exclude_options_), opt);
        if(iterator == std::end(exclude_options_)) {
            return false;
        }
        exclude_options_.erase(iterator);
        return true;
    }

    /// Removes a subcommand from the excludes list of this subcommand
    bool remove_excludes(App *app) {
        auto iterator = std::find(std::begin(exclude_subcommands_), std::end(exclude_subcommands_), app);
        if(iterator == std::end(exclude_subcommands_)) {
            return false;
        }
        auto other_app = *iterator;
        exclude_subcommands_.erase(iterator);
        other_app->remove_excludes(this);
        return true;
    }

    /// Removes an option from the needs list of this subcommand
    bool remove_needs(Option *opt) {
        auto iterator = std::find(std::begin(need_options_), std::end(need_options_), opt);
        if(iterator == std::end(need_options_)) {
            return false;
        }
        need_options_.erase(iterator);
        return true;
    }

    /// Removes a subcommand from the needs list of this subcommand
    bool remove_needs(App *app) {
        auto iterator = std::find(std::begin(need_subcommands_), std::end(need_subcommands_), app);
        if(iterator == std::end(need_subcommands_)) {
            return false;
        }
        need_subcommands_.erase(iterator);
        return true;
    }

    ///@}
    /// @name Help
    ///@{

    /// Set footer.
    App *footer(std::string footer_string) {
        footer_ = std::move(footer_string);
        return this;
    }
    /// Set footer.
    App *footer(std::function<std::string()> footer_function) {
        footer_callback_ = std::move(footer_function);
        return this;
    }
    /// Produce a string that could be read in as a config of the current values of the App. Set default_also to
    /// include default arguments. write_descriptions will print a description for the App and for each option.
    std::string config_to_str(bool default_also = false, bool write_description = false) const {
        return config_formatter_->to_config(this, default_also, write_description, "");
    }

    /// Makes a help message, using the currently configured formatter
    /// Will only do one subcommand at a time
    std::string help(std::string prev = "", AppFormatMode mode = AppFormatMode::Normal) const {
        if(prev.empty())
            prev = get_name();
        else
            prev += " " + get_name();

        // Delegate to subcommand if needed
        auto selected_subcommands = get_subcommands();
        if(!selected_subcommands.empty()) {
            return selected_subcommands.at(0)->help(prev, mode);
        }
        return formatter_->make_help(this, prev, mode);
    }

    /// Displays a version string
    std::string version() const {
        std::string val;
        if(version_ptr_ != nullptr) {
            auto rv = version_ptr_->results();
            version_ptr_->clear();
            version_ptr_->add_result("true");
            try {
                version_ptr_->run_callback();
            } catch(const CLI::CallForVersion &cfv) {
                val = cfv.what();
            }
            version_ptr_->clear();
            version_ptr_->add_result(rv);
        }
        return val;
    }
    ///@}
    /// @name Getters
    ///@{

    /// Access the formatter
    std::shared_ptr<FormatterBase> get_formatter() const { return formatter_; }

    /// Access the config formatter
    std::shared_ptr<Config> get_config_formatter() const { return config_formatter_; }

    /// Access the config formatter as a configBase pointer
    std::shared_ptr<ConfigBase> get_config_formatter_base() const {
        // This is safer as a dynamic_cast if we have RTTI, as Config -> ConfigBase
#if CLI11_USE_STATIC_RTTI == 0
        return std::dynamic_pointer_cast<ConfigBase>(config_formatter_);
#else
        return std::static_pointer_cast<ConfigBase>(config_formatter_);
#endif
    }

    /// Get the app or subcommand description
    std::string get_description() const { return description_; }

    /// Set the description of the app
    App *description(std::string app_description) {
        description_ = std::move(app_description);
        return this;
    }

    /// Get the list of options (user facing function, so returns raw pointers), has optional filter function
    std::vector<const Option *> get_options(const std::function<bool(const Option *)> filter = {}) const {
        std::vector<const Option *> options(options_.size());
        std::transform(std::begin(options_), std::end(options_), std::begin(options), [](const Option_p &val) {
            return val.get();
        });

        if(filter) {
            options.erase(std::remove_if(std::begin(options),
                                         std::end(options),
                                         [&filter](const Option *opt) { return !filter(opt); }),
                          std::end(options));
        }

        return options;
    }

    /// Non-const version of the above
    std::vector<Option *> get_options(const std::function<bool(Option *)> filter = {}) {
        std::vector<Option *> options(options_.size());
        std::transform(std::begin(options_), std::end(options_), std::begin(options), [](const Option_p &val) {
            return val.get();
        });

        if(filter) {
            options.erase(
                std::remove_if(std::begin(options), std::end(options), [&filter](Option *opt) { return !filter(opt); }),
                std::end(options));
        }

        return options;
    }

    /// Get an option by name (noexcept non-const version)
    Option *get_option_no_throw(std::string option_name) noexcept {
        for(Option_p &opt : options_) {
            if(opt->check_name(option_name)) {
                return opt.get();
            }
        }
        for(auto &subc : subcommands_) {
            // also check down into nameless subcommands
            if(subc->get_name().empty()) {
                auto opt = subc->get_option_no_throw(option_name);
                if(opt != nullptr) {
                    return opt;
                }
            }
        }
        return nullptr;
    }

    /// Get an option by name (noexcept const version)
    const Option *get_option_no_throw(std::string option_name) const noexcept {
        for(const Option_p &opt : options_) {
            if(opt->check_name(option_name)) {
                return opt.get();
            }
        }
        for(const auto &subc : subcommands_) {
            // also check down into nameless subcommands
            if(subc->get_name().empty()) {
                auto opt = subc->get_option_no_throw(option_name);
                if(opt != nullptr) {
                    return opt;
                }
            }
        }
        return nullptr;
    }

    /// Get an option by name
    const Option *get_option(std::string option_name) const {
        auto opt = get_option_no_throw(option_name);
        if(opt == nullptr) {
            throw OptionNotFound(option_name);
        }
        return opt;
    }

    /// Get an option by name (non-const version)
    Option *get_option(std::string option_name) {
        auto opt = get_option_no_throw(option_name);
        if(opt == nullptr) {
            throw OptionNotFound(option_name);
        }
        return opt;
    }

    /// Shortcut bracket operator for getting a pointer to an option
    const Option *operator[](const std::string &option_name) const { return get_option(option_name); }

    /// Shortcut bracket operator for getting a pointer to an option
    const Option *operator[](const char *option_name) const { return get_option(option_name); }

    /// Check the status of ignore_case
    bool get_ignore_case() const { return ignore_case_; }

    /// Check the status of ignore_underscore
    bool get_ignore_underscore() const { return ignore_underscore_; }

    /// Check the status of fallthrough
    bool get_fallthrough() const { return fallthrough_; }

    /// Check the status of the allow windows style options
    bool get_allow_windows_style_options() const { return allow_windows_style_options_; }

    /// Check the status of the allow windows style options
    bool get_positionals_at_end() const { return positionals_at_end_; }

    /// Check the status of the allow windows style options
    bool get_configurable() const { return configurable_; }

    /// Get the group of this subcommand
    const std::string &get_group() const { return group_; }

    /// Generate and return the footer.
    std::string get_footer() const { return (footer_callback_) ? footer_callback_() + '\n' + footer_ : footer_; }

    /// Get the required min subcommand value
    std::size_t get_require_subcommand_min() const { return require_subcommand_min_; }

    /// Get the required max subcommand value
    std::size_t get_require_subcommand_max() const { return require_subcommand_max_; }

    /// Get the required min option value
    std::size_t get_require_option_min() const { return require_option_min_; }

    /// Get the required max option value
    std::size_t get_require_option_max() const { return require_option_max_; }

    /// Get the prefix command status
    bool get_prefix_command() const { return prefix_command_; }

    /// Get the status of allow extras
    bool get_allow_extras() const { return allow_extras_; }

    /// Get the status of required
    bool get_required() const { return required_; }

    /// Get the status of disabled
    bool get_disabled() const { return disabled_; }

    /// Get the status of silence
    bool get_silent() const { return silent_; }

    /// Get the status of disabled
    bool get_immediate_callback() const { return immediate_callback_; }

    /// Get the status of disabled by default
    bool get_disabled_by_default() const { return (default_startup == startup_mode::disabled); }

    /// Get the status of disabled by default
    bool get_enabled_by_default() const { return (default_startup == startup_mode::enabled); }
    /// Get the status of validating positionals
    bool get_validate_positionals() const { return validate_positionals_; }
    /// Get the status of validating optional vector arguments
    bool get_validate_optional_arguments() const { return validate_optional_arguments_; }

    /// Get the status of allow extras
    config_extras_mode get_allow_config_extras() const { return allow_config_extras_; }

    /// Get a pointer to the help flag.
    Option *get_help_ptr() { return help_ptr_; }

    /// Get a pointer to the help flag. (const)
    const Option *get_help_ptr() const { return help_ptr_; }

    /// Get a pointer to the help all flag. (const)
    const Option *get_help_all_ptr() const { return help_all_ptr_; }

    /// Get a pointer to the config option.
    Option *get_config_ptr() { return config_ptr_; }

    /// Get a pointer to the config option. (const)
    const Option *get_config_ptr() const { return config_ptr_; }

    /// Get a pointer to the version option.
    Option *get_version_ptr() { return version_ptr_; }

    /// Get a pointer to the version option. (const)
    const Option *get_version_ptr() const { return version_ptr_; }

    /// Get the parent of this subcommand (or nullptr if main app)
    App *get_parent() { return parent_; }

    /// Get the parent of this subcommand (or nullptr if main app) (const version)
    const App *get_parent() const { return parent_; }

    /// Get the name of the current app
    const std::string &get_name() const { return name_; }

    /// Get the aliases of the current app
    const std::vector<std::string> &get_aliases() const { return aliases_; }

    /// clear all the aliases of the current App
    App *clear_aliases() {
        aliases_.clear();
        return this;
    }

    /// Get a display name for an app
    std::string get_display_name(bool with_aliases = false) const {
        if(name_.empty()) {
            return std::string("[Option Group: ") + get_group() + "]";
        }
        if(aliases_.empty() || !with_aliases) {
            return name_;
        }
        std::string dispname = name_;
        for(const auto &lalias : aliases_) {
            dispname.push_back(',');
            dispname.push_back(' ');
            dispname.append(lalias);
        }
        return dispname;
    }

    /// Check the name, case insensitive and underscore insensitive if set
    bool check_name(std::string name_to_check) const {
        std::string local_name = name_;
        if(ignore_underscore_) {
            local_name = detail::remove_underscore(name_);
            name_to_check = detail::remove_underscore(name_to_check);
        }
        if(ignore_case_) {
            local_name = detail::to_lower(name_);
            name_to_check = detail::to_lower(name_to_check);
        }

        if(local_name == name_to_check) {
            return true;
        }
        for(auto les : aliases_) {
            if(ignore_underscore_) {
                les = detail::remove_underscore(les);
            }
            if(ignore_case_) {
                les = detail::to_lower(les);
            }
            if(les == name_to_check) {
                return true;
            }
        }
        return false;
    }

    /// Get the groups available directly from this option (in order)
    std::vector<std::string> get_groups() const {
        std::vector<std::string> groups;

        for(const Option_p &opt : options_) {
            // Add group if it is not already in there
            if(std::find(groups.begin(), groups.end(), opt->get_group()) == groups.end()) {
                groups.push_back(opt->get_group());
            }
        }

        return groups;
    }

    /// This gets a vector of pointers with the original parse order
    const std::vector<Option *> &parse_order() const { return parse_order_; }

    /// This returns the missing options from the current subcommand
    std::vector<std::string> remaining(bool recurse = false) const {
        std::vector<std::string> miss_list;
        for(const std::pair<detail::Classifier, std::string> &miss : missing_) {
            miss_list.push_back(std::get<1>(miss));
        }
        // Get from a subcommand that may allow extras
        if(recurse) {
            if(!allow_extras_) {
                for(const auto &sub : subcommands_) {
                    if(sub->name_.empty() && !sub->missing_.empty()) {
                        for(const std::pair<detail::Classifier, std::string> &miss : sub->missing_) {
                            miss_list.push_back(std::get<1>(miss));
                        }
                    }
                }
            }
            // Recurse into subcommands

            for(const App *sub : parsed_subcommands_) {
                std::vector<std::string> output = sub->remaining(recurse);
                std::copy(std::begin(output), std::end(output), std::back_inserter(miss_list));
            }
        }
        return miss_list;
    }

    /// This returns the missing options in a form ready for processing by another command line program
    std::vector<std::string> remaining_for_passthrough(bool recurse = false) const {
        std::vector<std::string> miss_list = remaining(recurse);
        std::reverse(std::begin(miss_list), std::end(miss_list));
        return miss_list;
    }

    /// This returns the number of remaining options, minus the -- separator
    std::size_t remaining_size(bool recurse = false) const {
        auto remaining_options = static_cast<std::size_t>(std::count_if(
            std::begin(missing_), std::end(missing_), [](const std::pair<detail::Classifier, std::string> &val) {
                return val.first != detail::Classifier::POSITIONAL_MARK;
            }));

        if(recurse) {
            for(const App_p &sub : subcommands_) {
                remaining_options += sub->remaining_size(recurse);
            }
        }
        return remaining_options;
    }

    ///@}

  protected:
    /// Check the options to make sure there are no conflicts.
    ///
    /// Currently checks to see if multiple positionals exist with unlimited args and checks if the min and max options
    /// are feasible
    void _validate() const {
        // count the number of positional only args
        auto pcount = std::count_if(std::begin(options_), std::end(options_), [](const Option_p &opt) {
            return opt->get_items_expected_max() >= detail::expected_max_vector_size && !opt->nonpositional();
        });
        if(pcount > 1) {
            auto pcount_req = std::count_if(std::begin(options_), std::end(options_), [](const Option_p &opt) {
                return opt->get_items_expected_max() >= detail::expected_max_vector_size && !opt->nonpositional() &&
                       opt->get_required();
            });
            if(pcount - pcount_req > 1) {
                throw InvalidError(name_);
            }
        }

        std::size_t nameless_subs{0};
        for(const App_p &app : subcommands_) {
            app->_validate();
            if(app->get_name().empty())
                ++nameless_subs;
        }

        if(require_option_min_ > 0) {
            if(require_option_max_ > 0) {
                if(require_option_max_ < require_option_min_) {
                    throw(InvalidError("Required min options greater than required max options",
                                       ExitCodes::InvalidError));
                }
            }
            if(require_option_min_ > (options_.size() + nameless_subs)) {
                throw(InvalidError("Required min options greater than number of available options",
                                   ExitCodes::InvalidError));
            }
        }
    }

    /// configure subcommands to enable parsing through the current object
    /// set the correct fallthrough and prefix for nameless subcommands and manage the automatic enable or disable
    /// makes sure parent is set correctly
    void _configure() {
        if(default_startup == startup_mode::enabled) {
            disabled_ = false;
        } else if(default_startup == startup_mode::disabled) {
            disabled_ = true;
        }
        for(const App_p &app : subcommands_) {
            if(app->has_automatic_name_) {
                app->name_.clear();
            }
            if(app->name_.empty()) {
                app->fallthrough_ = false;  // make sure fallthrough_ is false to prevent infinite loop
                app->prefix_command_ = false;
            }
            // make sure the parent is set to be this object in preparation for parse
            app->parent_ = this;
            app->_configure();
        }
    }

    /// Internal function to run (App) callback, bottom up
    void run_callback(bool final_mode = false, bool suppress_final_callback = false) {
        pre_callback();
        // in the main app if immediate_callback_ is set it runs the main callback before the used subcommands
        if(!final_mode && parse_complete_callback_) {
            parse_complete_callback_();
        }
        // run the callbacks for the received subcommands
        for(App *subc : get_subcommands()) {
            if(subc->parent_ == this) {
                subc->run_callback(true, suppress_final_callback);
            }
        }
        // now run callbacks for option_groups
        for(auto &subc : subcommands_) {
            if(subc->name_.empty() && subc->count_all() > 0) {
                subc->run_callback(true, suppress_final_callback);
            }
        }

        // finally run the main callback
        if(final_callback_ && (parsed_ > 0) && (!suppress_final_callback)) {
            if(!name_.empty() || count_all() > 0 || parent_ == nullptr) {
                final_callback_();
            }
        }
    }

    /// Check to see if a subcommand is valid. Give up immediately if subcommand max has been reached.
    bool _valid_subcommand(const std::string &current, bool ignore_used = true) const {
        // Don't match if max has been reached - but still check parents
        if(require_subcommand_max_ != 0 && parsed_subcommands_.size() >= require_subcommand_max_) {
            return parent_ != nullptr && parent_->_valid_subcommand(current, ignore_used);
        }
        auto com = _find_subcommand(current, true, ignore_used);
        if(com != nullptr) {
            return true;
        }
        // Check parent if exists, else return false
        return parent_ != nullptr && parent_->_valid_subcommand(current, ignore_used);
    }

    /// Selects a Classifier enum based on the type of the current argument
    detail::Classifier _recognize(const std::string &current, bool ignore_used_subcommands = true) const {
        std::string dummy1, dummy2;

        if(current == "--")
            return detail::Classifier::POSITIONAL_MARK;
        if(_valid_subcommand(current, ignore_used_subcommands))
            return detail::Classifier::SUBCOMMAND;
        if(detail::split_long(current, dummy1, dummy2))
            return detail::Classifier::LONG;
        if(detail::split_short(current, dummy1, dummy2)) {
            if(dummy1[0] >= '0' && dummy1[0] <= '9') {
                if(get_option_no_throw(std::string{'-', dummy1[0]}) == nullptr) {
                    return detail::Classifier::NONE;
                }
            }
            return detail::Classifier::SHORT;
        }
        if((allow_windows_style_options_) && (detail::split_windows_style(current, dummy1, dummy2)))
            return detail::Classifier::WINDOWS_STYLE;
        if((current == "++") && !name_.empty() && parent_ != nullptr)
            return detail::Classifier::SUBCOMMAND_TERMINATOR;
        return detail::Classifier::NONE;
    }

    // The parse function is now broken into several parts, and part of process

    /// Read and process a configuration file (main app only)
    void _process_config_file() {
        if(config_ptr_ != nullptr) {
            bool config_required = config_ptr_->get_required();
            auto file_given = config_ptr_->count() > 0;
            auto config_files = config_ptr_->as<std::vector<std::string>>();
            if(config_files.empty() || config_files.front().empty()) {
                if(config_required) {
                    throw FileError::Missing("no specified config file");
                }
                return;
            }
            for(auto rit = config_files.rbegin(); rit != config_files.rend(); ++rit) {
                const auto &config_file = *rit;
                auto path_result = detail::check_path(config_file.c_str());
                if(path_result == detail::path_type::file) {
                    try {
                        std::vector<ConfigItem> values = config_formatter_->from_file(config_file);
                        _parse_config(values);
                        if(!file_given) {
                            config_ptr_->add_result(config_file);
                        }
                    } catch(const FileError &) {
                        if(config_required || file_given)
                            throw;
                    }
                } else if(config_required || file_given) {
                    throw FileError::Missing(config_file);
                }
            }
        }
    }

    /// Get envname options if not yet passed. Runs on *all* subcommands.
    void _process_env() {
        for(const Option_p &opt : options_) {
            if(opt->count() == 0 && !opt->envname_.empty()) {
                char *buffer = nullptr;
                std::string ename_string;

#ifdef _MSC_VER
                // Windows version
                std::size_t sz = 0;
                if(_dupenv_s(&buffer, &sz, opt->envname_.c_str()) == 0 && buffer != nullptr) {
                    ename_string = std::string(buffer);
                    free(buffer);
                }
#else
                // This also works on Windows, but gives a warning
                buffer = std::getenv(opt->envname_.c_str());
                if(buffer != nullptr)
                    ename_string = std::string(buffer);
#endif

                if(!ename_string.empty()) {
                    opt->add_result(ename_string);
                }
            }
        }

        for(App_p &sub : subcommands_) {
            if(sub->get_name().empty() || !sub->parse_complete_callback_)
                sub->_process_env();
        }
    }

    /// Process callbacks. Runs on *all* subcommands.
    void _process_callbacks() {

        for(App_p &sub : subcommands_) {
            // process the priority option_groups first
            if(sub->get_name().empty() && sub->parse_complete_callback_) {
                if(sub->count_all() > 0) {
                    sub->_process_callbacks();
                    sub->run_callback();
                }
            }
        }

        for(const Option_p &opt : options_) {
            if((*opt) && !opt->get_callback_run()) {
                opt->run_callback();
            }
        }
        for(App_p &sub : subcommands_) {
            if(!sub->parse_complete_callback_) {
                sub->_process_callbacks();
            }
        }
    }

    /// Run help flag processing if any are found.
    ///
    /// The flags allow recursive calls to remember if there was a help flag on a parent.
    void _process_help_flags(bool trigger_help = false, bool trigger_all_help = false) const {
        const Option *help_ptr = get_help_ptr();
        const Option *help_all_ptr = get_help_all_ptr();

        if(help_ptr != nullptr && help_ptr->count() > 0)
            trigger_help = true;
        if(help_all_ptr != nullptr && help_all_ptr->count() > 0)
            trigger_all_help = true;

        // If there were parsed subcommands, call those. First subcommand wins if there are multiple ones.
        if(!parsed_subcommands_.empty()) {
            for(const App *sub : parsed_subcommands_)
                sub->_process_help_flags(trigger_help, trigger_all_help);

            // Only the final subcommand should call for help. All help wins over help.
        } else if(trigger_all_help) {
            throw CallForAllHelp();
        } else if(trigger_help) {
            throw CallForHelp();
        }
    }

    /// Verify required options and cross requirements. Subcommands too (only if selected).
    void _process_requirements() {
        // check excludes
        bool excluded{false};
        std::string excluder;
        for(auto &opt : exclude_options_) {
            if(opt->count() > 0) {
                excluded = true;
                excluder = opt->get_name();
            }
        }
        for(auto &subc : exclude_subcommands_) {
            if(subc->count_all() > 0) {
                excluded = true;
                excluder = subc->get_display_name();
            }
        }
        if(excluded) {
            if(count_all() > 0) {
                throw ExcludesError(get_display_name(), excluder);
            }
            // if we are excluded but didn't receive anything, just return
            return;
        }

        // check excludes
        bool missing_needed{false};
        std::string missing_need;
        for(auto &opt : need_options_) {
            if(opt->count() == 0) {
                missing_needed = true;
                missing_need = opt->get_name();
            }
        }
        for(auto &subc : need_subcommands_) {
            if(subc->count_all() == 0) {
                missing_needed = true;
                missing_need = subc->get_display_name();
            }
        }
        if(missing_needed) {
            if(count_all() > 0) {
                throw RequiresError(get_display_name(), missing_need);
            }
            // if we missing something but didn't have any options, just return
            return;
        }

        std::size_t used_options = 0;
        for(const Option_p &opt : options_) {

            if(opt->count() != 0) {
                ++used_options;
            }
            // Required but empty
            if(opt->get_required() && opt->count() == 0) {
                throw RequiredError(opt->get_name());
            }
            // Requires
            for(const Option *opt_req : opt->needs_)
                if(opt->count() > 0 && opt_req->count() == 0)
                    throw RequiresError(opt->get_name(), opt_req->get_name());
            // Excludes
            for(const Option *opt_ex : opt->excludes_)
                if(opt->count() > 0 && opt_ex->count() != 0)
                    throw ExcludesError(opt->get_name(), opt_ex->get_name());
        }
        // check for the required number of subcommands
        if(require_subcommand_min_ > 0) {
            auto selected_subcommands = get_subcommands();
            if(require_subcommand_min_ > selected_subcommands.size())
                throw RequiredError::Subcommand(require_subcommand_min_);
        }

        // Max error cannot occur, the extra subcommand will parse as an ExtrasError or a remaining item.

        // run this loop to check how many unnamed subcommands were actually used since they are considered options
        // from the perspective of an App
        for(App_p &sub : subcommands_) {
            if(sub->disabled_)
                continue;
            if(sub->name_.empty() && sub->count_all() > 0) {
                ++used_options;
            }
        }

        if(require_option_min_ > used_options || (require_option_max_ > 0 && require_option_max_ < used_options)) {
            auto option_list = detail::join(options_, [this](const Option_p &ptr) {
                if(ptr.get() == help_ptr_ || ptr.get() == help_all_ptr_) {
                    return std::string{};
                }
                return ptr->get_name(false, true);
            });

            auto subc_list = get_subcommands([](App *app) { return ((app->get_name().empty()) && (!app->disabled_)); });
            if(!subc_list.empty()) {
                option_list += "," + detail::join(subc_list, [](const App *app) { return app->get_display_name(); });
            }
            throw RequiredError::Option(require_option_min_, require_option_max_, used_options, option_list);
        }

        // now process the requirements for subcommands if needed
        for(App_p &sub : subcommands_) {
            if(sub->disabled_)
                continue;
            if(sub->name_.empty() && sub->required_ == false) {
                if(sub->count_all() == 0) {
                    if(require_option_min_ > 0 && require_option_min_ <= used_options) {
                        continue;
                        // if we have met the requirement and there is nothing in this option group skip checking
                        // requirements
                    }
                    if(require_option_max_ > 0 && used_options >= require_option_min_) {
                        continue;
                        // if we have met the requirement and there is nothing in this option group skip checking
                        // requirements
                    }
                }
            }
            if(sub->count() > 0 || sub->name_.empty()) {
                sub->_process_requirements();
            }

            if(sub->required_ && sub->count_all() == 0) {
                throw(CLI::RequiredError(sub->get_display_name()));
            }
        }
    }

    /// Process callbacks and such.
    void _process() {
        try {
            // the config file might generate a FileError but that should not be processed until later in the process
            // to allow for help, version and other errors to generate first.
            _process_config_file();

            // process env shouldn't throw but no reason to process it if config generated an error
            _process_env();
        } catch(const CLI::FileError &) {
            // callbacks and help_flags can generate exceptions which should take priority
            // over the config file error if one exists.
            _process_callbacks();
            _process_help_flags();
            throw;
        }

        _process_callbacks();
        _process_help_flags();

        _process_requirements();
    }

    /// Throw an error if anything is left over and should not be.
    void _process_extras() {
        if(!(allow_extras_ || prefix_command_)) {
            std::size_t num_left_over = remaining_size();
            if(num_left_over > 0) {
                throw ExtrasError(name_, remaining(false));
            }
        }

        for(App_p &sub : subcommands_) {
            if(sub->count() > 0)
                sub->_process_extras();
        }
    }

    /// Throw an error if anything is left over and should not be.
    /// Modifies the args to fill in the missing items before throwing.
    void _process_extras(std::vector<std::string> &args) {
        if(!(allow_extras_ || prefix_command_)) {
            std::size_t num_left_over = remaining_size();
            if(num_left_over > 0) {
                args = remaining(false);
                throw ExtrasError(name_, args);
            }
        }

        for(App_p &sub : subcommands_) {
            if(sub->count() > 0)
                sub->_process_extras(args);
        }
    }

    /// Internal function to recursively increment the parsed counter on the current app as well unnamed subcommands
    void increment_parsed() {
        ++parsed_;
        for(App_p &sub : subcommands_) {
            if(sub->get_name().empty())
                sub->increment_parsed();
        }
    }
    /// Internal parse function
    void _parse(std::vector<std::string> &args) {
        increment_parsed();
        _trigger_pre_parse(args.size());
        bool positional_only = false;

        while(!args.empty()) {
            if(!_parse_single(args, positional_only)) {
                break;
            }
        }

        if(parent_ == nullptr) {
            _process();

            // Throw error if any items are left over (depending on settings)
            _process_extras(args);

            // Convert missing (pairs) to extras (string only) ready for processing in another app
            args = remaining_for_passthrough(false);
        } else if(parse_complete_callback_) {
            _process_env();
            _process_callbacks();
            _process_help_flags();
            _process_requirements();
            run_callback(false, true);
        }
    }

    /// Internal parse function
    void _parse(std::vector<std::string> &&args) {
        // this can only be called by the top level in which case parent == nullptr by definition
        // operation is simplified
        increment_parsed();
        _trigger_pre_parse(args.size());
        bool positional_only = false;

        while(!args.empty()) {
            _parse_single(args, positional_only);
        }
        _process();

        // Throw error if any items are left over (depending on settings)
        _process_extras();
    }

    /// Internal function to parse a stream
    void _parse_stream(std::istream &input) {
        auto values = config_formatter_->from_config(input);
        _parse_config(values);
        increment_parsed();
        _trigger_pre_parse(values.size());
        _process();

        // Throw error if any items are left over (depending on settings)
        _process_extras();
    }

    /// Parse one config param, return false if not found in any subcommand, remove if it is
    ///
    /// If this has more than one dot.separated.name, go into the subcommand matching it
    /// Returns true if it managed to find the option, if false you'll need to remove the arg manually.
    void _parse_config(const std::vector<ConfigItem> &args) {
        for(const ConfigItem &item : args) {
            if(!_parse_single_config(item) && allow_config_extras_ == config_extras_mode::error)
                throw ConfigError::Extras(item.fullname());
        }
    }

    /// Fill in a single config option
    bool _parse_single_config(const ConfigItem &item, std::size_t level = 0) {
        if(level < item.parents.size()) {
            try {
                auto subcom = get_subcommand(item.parents.at(level));
                auto result = subcom->_parse_single_config(item, level + 1);

                return result;
            } catch(const OptionNotFound &) {
                return false;
            }
        }
        // check for section open
        if(item.name == "++") {
            if(configurable_) {
                increment_parsed();
                _trigger_pre_parse(2);
                if(parent_ != nullptr) {
                    parent_->parsed_subcommands_.push_back(this);
                }
            }
            return true;
        }
        // check for section close
        if(item.name == "--") {
            if(configurable_) {
                _process_callbacks();
                _process_requirements();
                run_callback();
            }
            return true;
        }
        Option *op = get_option_no_throw("--" + item.name);
        if(op == nullptr) {
            if(item.name.size() == 1) {
                op = get_option_no_throw("-" + item.name);
            }
        }
        if(op == nullptr) {
            op = get_option_no_throw(item.name);
        }
        if(op == nullptr) {
            // If the option was not present
            if(get_allow_config_extras() == config_extras_mode::capture)
                // Should we worry about classifying the extras properly?
                missing_.emplace_back(detail::Classifier::NONE, item.fullname());
            return false;
        }

        if(!op->get_configurable()) {
            if(get_allow_config_extras() == config_extras_mode::ignore_all) {
                return false;
            }
            throw ConfigError::NotConfigurable(item.fullname());
        }

        if(op->empty()) {

            if(op->get_expected_min() == 0) {
                // Flag parsing
                auto res = config_formatter_->to_flag(item);
                res = op->get_flag_value(item.name, res);

                op->add_result(res);

            } else {
                op->add_result(item.inputs);
                op->run_callback();
            }
        }

        return true;
    }

    /// Parse "one" argument (some may eat more than one), delegate to parent if fails, add to missing if missing
    /// from main return false if the parse has failed and needs to return to parent
    bool _parse_single(std::vector<std::string> &args, bool &positional_only) {
        bool retval = true;
        detail::Classifier classifier = positional_only ? detail::Classifier::NONE : _recognize(args.back());
        switch(classifier) {
        case detail::Classifier::POSITIONAL_MARK:
            args.pop_back();
            positional_only = true;
            if((!_has_remaining_positionals()) && (parent_ != nullptr)) {
                retval = false;
            } else {
                _move_to_missing(classifier, "--");
            }
            break;
        case detail::Classifier::SUBCOMMAND_TERMINATOR:
            // treat this like a positional mark if in the parent app
            args.pop_back();
            retval = false;
            break;
        case detail::Classifier::SUBCOMMAND:
            retval = _parse_subcommand(args);
            break;
        case detail::Classifier::LONG:
        case detail::Classifier::SHORT:
        case detail::Classifier::WINDOWS_STYLE:
            // If already parsed a subcommand, don't accept options_
            _parse_arg(args, classifier);
            break;
        case detail::Classifier::NONE:
            // Probably a positional or something for a parent (sub)command
            retval = _parse_positional(args, false);
            if(retval && positionals_at_end_) {
                positional_only = true;
            }
            break;
            // LCOV_EXCL_START
        default:
            throw HorribleError("unrecognized classifier (you should not see this!)");
            // LCOV_EXCL_STOP
        }
        return retval;
    }

    /// Count the required remaining positional arguments
    std::size_t _count_remaining_positionals(bool required_only = false) const {
        std::size_t retval = 0;
        for(const Option_p &opt : options_) {
            if(opt->get_positional() && (!required_only || opt->get_required())) {
                if(opt->get_items_expected_min() > 0 &&
                   static_cast<int>(opt->count()) < opt->get_items_expected_min()) {
                    retval += static_cast<std::size_t>(opt->get_items_expected_min()) - opt->count();
                }
            }
        }
        return retval;
    }

    /// Count the required remaining positional arguments
    bool _has_remaining_positionals() const {
        for(const Option_p &opt : options_) {
            if(opt->get_positional() && ((static_cast<int>(opt->count()) < opt->get_items_expected_min()))) {
                return true;
            }
        }

        return false;
    }

    /// Parse a positional, go up the tree to check
    /// @param haltOnSubcommand if set to true the operation will not process subcommands merely return false
    /// Return true if the positional was used false otherwise
    bool _parse_positional(std::vector<std::string> &args, bool haltOnSubcommand) {

        const std::string &positional = args.back();

        if(positionals_at_end_) {
            // deal with the case of required arguments at the end which should take precedence over other arguments
            auto arg_rem = args.size();
            auto remreq = _count_remaining_positionals(true);
            if(arg_rem <= remreq) {
                for(const Option_p &opt : options_) {
                    if(opt->get_positional() && opt->required_) {
                        if(static_cast<int>(opt->count()) < opt->get_items_expected_min()) {
                            if(validate_positionals_) {
                                std::string pos = positional;
                                pos = opt->_validate(pos, 0);
                                if(!pos.empty()) {
                                    continue;
                                }
                            }

                            parse_order_.push_back(opt.get());
                            /// if we require a separator add it here
                            if(opt->get_inject_separator()) {
                                if(!opt->results().empty() && !opt->results().back().empty()) {
                                    opt->add_result(std::string{});
                                }
                            }
                            if(opt->get_trigger_on_parse() &&
                               opt->current_option_state_ == Option::option_state::callback_run) {
                                opt->clear();
                            }
                            opt->add_result(positional);
                            if(opt->get_trigger_on_parse()) {
                                opt->run_callback();
                            }
                            args.pop_back();
                            return true;
                        }
                    }
                }
            }
        }
        for(const Option_p &opt : options_) {
            // Eat options, one by one, until done
            if(opt->get_positional() &&
               (static_cast<int>(opt->count()) < opt->get_items_expected_min() || opt->get_allow_extra_args())) {
                if(validate_positionals_) {
                    std::string pos = positional;
                    pos = opt->_validate(pos, 0);
                    if(!pos.empty()) {
                        continue;
                    }
                }
                if(opt->get_inject_separator()) {
                    if(!opt->results().empty() && !opt->results().back().empty()) {
                        opt->add_result(std::string{});
                    }
                }
                if(opt->get_trigger_on_parse() && opt->current_option_state_ == Option::option_state::callback_run) {
                    opt->clear();
                }
                opt->add_result(positional);
                if(opt->get_trigger_on_parse()) {
                    opt->run_callback();
                }
                parse_order_.push_back(opt.get());
                args.pop_back();
                return true;
            }
        }

        for(auto &subc : subcommands_) {
            if((subc->name_.empty()) && (!subc->disabled_)) {
                if(subc->_parse_positional(args, false)) {
                    if(!subc->pre_parse_called_) {
                        subc->_trigger_pre_parse(args.size());
                    }
                    return true;
                }
            }
        }
        // let the parent deal with it if possible
        if(parent_ != nullptr && fallthrough_)
            return _get_fallthrough_parent()->_parse_positional(args, static_cast<bool>(parse_complete_callback_));

        /// Try to find a local subcommand that is repeated
        auto com = _find_subcommand(args.back(), true, false);
        if(com != nullptr && (require_subcommand_max_ == 0 || require_subcommand_max_ > parsed_subcommands_.size())) {
            if(haltOnSubcommand) {
                return false;
            }
            args.pop_back();
            com->_parse(args);
            return true;
        }
        /// now try one last gasp at subcommands that have been executed before, go to root app and try to find a
        /// subcommand in a broader way, if one exists let the parent deal with it
        auto parent_app = (parent_ != nullptr) ? _get_fallthrough_parent() : this;
        com = parent_app->_find_subcommand(args.back(), true, false);
        if(com != nullptr && (com->parent_->require_subcommand_max_ == 0 ||
                              com->parent_->require_subcommand_max_ > com->parent_->parsed_subcommands_.size())) {
            return false;
        }

        if(positionals_at_end_) {
            throw CLI::ExtrasError(name_, args);
        }
        /// If this is an option group don't deal with it
        if(parent_ != nullptr && name_.empty()) {
            return false;
        }
        /// We are out of other options this goes to missing
        _move_to_missing(detail::Classifier::NONE, positional);
        args.pop_back();
        if(prefix_command_) {
            while(!args.empty()) {
                _move_to_missing(detail::Classifier::NONE, args.back());
                args.pop_back();
            }
        }

        return true;
    }

    /// Locate a subcommand by name with two conditions, should disabled subcommands be ignored, and should used
    /// subcommands be ignored
    App *_find_subcommand(const std::string &subc_name, bool ignore_disabled, bool ignore_used) const noexcept {
        for(const App_p &com : subcommands_) {
            if(com->disabled_ && ignore_disabled)
                continue;
            if(com->get_name().empty()) {
                auto subc = com->_find_subcommand(subc_name, ignore_disabled, ignore_used);
                if(subc != nullptr) {
                    return subc;
                }
            }
            if(com->check_name(subc_name)) {
                if((!*com) || !ignore_used)
                    return com.get();
            }
        }
        return nullptr;
    }

    /// Parse a subcommand, modify args and continue
    ///
    /// Unlike the others, this one will always allow fallthrough
    /// return true if the subcommand was processed false otherwise
    bool _parse_subcommand(std::vector<std::string> &args) {
        if(_count_remaining_positionals(/* required */ true) > 0) {
            _parse_positional(args, false);
            return true;
        }
        auto com = _find_subcommand(args.back(), true, true);
        if(com != nullptr) {
            args.pop_back();
            if(!com->silent_) {
                parsed_subcommands_.push_back(com);
            }
            com->_parse(args);
            auto parent_app = com->parent_;
            while(parent_app != this) {
                parent_app->_trigger_pre_parse(args.size());
                if(!com->silent_) {
                    parent_app->parsed_subcommands_.push_back(com);
                }
                parent_app = parent_app->parent_;
            }
            return true;
        }

        if(parent_ == nullptr)
            throw HorribleError("Subcommand " + args.back() + " missing");
        return false;
    }

    /// Parse a short (false) or long (true) argument, must be at the top of the list
    /// return true if the argument was processed or false if nothing was done
    bool _parse_arg(std::vector<std::string> &args, detail::Classifier current_type) {

        std::string current = args.back();

        std::string arg_name;
        std::string value;
        std::string rest;

        switch(current_type) {
        case detail::Classifier::LONG:
            if(!detail::split_long(current, arg_name, value))
                throw HorribleError("Long parsed but missing (you should not see this):" + args.back());
            break;
        case detail::Classifier::SHORT:
            if(!detail::split_short(current, arg_name, rest))
                throw HorribleError("Short parsed but missing! You should not see this");
            break;
        case detail::Classifier::WINDOWS_STYLE:
            if(!detail::split_windows_style(current, arg_name, value))
                throw HorribleError("windows option parsed but missing! You should not see this");
            break;
        case detail::Classifier::SUBCOMMAND:
        case detail::Classifier::SUBCOMMAND_TERMINATOR:
        case detail::Classifier::POSITIONAL_MARK:
        case detail::Classifier::NONE:
        default:
            throw HorribleError("parsing got called with invalid option! You should not see this");
        }

        auto op_ptr =
            std::find_if(std::begin(options_), std::end(options_), [arg_name, current_type](const Option_p &opt) {
                if(current_type == detail::Classifier::LONG)
                    return opt->check_lname(arg_name);
                if(current_type == detail::Classifier::SHORT)
                    return opt->check_sname(arg_name);
                // this will only get called for detail::Classifier::WINDOWS_STYLE
                return opt->check_lname(arg_name) || opt->check_sname(arg_name);
            });

        // Option not found
        if(op_ptr == std::end(options_)) {
            for(auto &subc : subcommands_) {
                if(subc->name_.empty() && !subc->disabled_) {
                    if(subc->_parse_arg(args, current_type)) {
                        if(!subc->pre_parse_called_) {
                            subc->_trigger_pre_parse(args.size());
                        }
                        return true;
                    }
                }
            }

            // don't capture missing if this is a nameless subcommand and nameless subcommands can't fallthrough
            if(parent_ != nullptr && name_.empty()) {
                return false;
            }

            // If a subcommand, try the main command
            if(parent_ != nullptr && fallthrough_)
                return _get_fallthrough_parent()->_parse_arg(args, current_type);

            // Otherwise, add to missing
            args.pop_back();
            _move_to_missing(current_type, current);
            return true;
        }

        args.pop_back();

        // Get a reference to the pointer to make syntax bearable
        Option_p &op = *op_ptr;
        /// if we require a separator add it here
        if(op->get_inject_separator()) {
            if(!op->results().empty() && !op->results().back().empty()) {
                op->add_result(std::string{});
            }
        }
        if(op->get_trigger_on_parse() && op->current_option_state_ == Option::option_state::callback_run) {
            op->clear();
        }
        int min_num = (std::min)(op->get_type_size_min(), op->get_items_expected_min());
        int max_num = op->get_items_expected_max();
        // check container like options to limit the argument size to a single type if the allow_extra_flags argument is
        // set. 16 is somewhat arbitrary (needs to be at least 4)
        if(max_num >= detail::expected_max_vector_size / 16 && !op->get_allow_extra_args()) {
            auto tmax = op->get_type_size_max();
            max_num = detail::checked_multiply(tmax, op->get_expected_min()) ? tmax : detail::expected_max_vector_size;
        }
        // Make sure we always eat the minimum for unlimited vectors
        int collected = 0;     // total number of arguments collected
        int result_count = 0;  // local variable for number of results in a single arg string
        // deal with purely flag like things
        if(max_num == 0) {
            auto res = op->get_flag_value(arg_name, value);
            op->add_result(res);
            parse_order_.push_back(op.get());
        } else if(!value.empty()) {  // --this=value
            op->add_result(value, result_count);
            parse_order_.push_back(op.get());
            collected += result_count;
            // -Trest
        } else if(!rest.empty()) {
            op->add_result(rest, result_count);
            parse_order_.push_back(op.get());
            rest = "";
            collected += result_count;
        }

        // gather the minimum number of arguments
        while(min_num > collected && !args.empty()) {
            std::string current_ = args.back();
            args.pop_back();
            op->add_result(current_, result_count);
            parse_order_.push_back(op.get());
            collected += result_count;
        }

        if(min_num > collected) {  // if we have run out of arguments and the minimum was not met
            throw ArgumentMismatch::TypedAtLeast(op->get_name(), min_num, op->get_type_name());
        }

        // now check for optional arguments
        if(max_num > collected || op->get_allow_extra_args()) {  // we allow optional arguments
            auto remreqpos = _count_remaining_positionals(true);
            // we have met the minimum now optionally check up to the maximum
            while((collected < max_num || op->get_allow_extra_args()) && !args.empty() &&
                  _recognize(args.back(), false) == detail::Classifier::NONE) {
                // If any required positionals remain, don't keep eating
                if(remreqpos >= args.size()) {
                    break;
                }
                if(validate_optional_arguments_) {
                    std::string optarg = args.back();
                    optarg = op->_validate(optarg, 0);
                    if(!optarg.empty()) {
                        break;
                    }
                }
                op->add_result(args.back(), result_count);
                parse_order_.push_back(op.get());
                args.pop_back();
                collected += result_count;
            }

            // Allow -- to end an unlimited list and "eat" it
            if(!args.empty() && _recognize(args.back()) == detail::Classifier::POSITIONAL_MARK)
                args.pop_back();
            // optional flag that didn't receive anything now get the default value
            if(min_num == 0 && max_num > 0 && collected == 0) {
                auto res = op->get_flag_value(arg_name, std::string{});
                op->add_result(res);
                parse_order_.push_back(op.get());
            }
        }
        // if we only partially completed a type then add an empty string if allowed for later processing
        if(min_num > 0 && (collected % op->get_type_size_max()) != 0) {
            if(op->get_type_size_max() != op->get_type_size_min()) {
                op->add_result(std::string{});
            } else {
                throw ArgumentMismatch::PartialType(op->get_name(), op->get_type_size_min(), op->get_type_name());
            }
        }
        if(op->get_trigger_on_parse()) {
            op->run_callback();
        }
        if(!rest.empty()) {
            rest = "-" + rest;
            args.push_back(rest);
        }
        return true;
    }

    /// Trigger the pre_parse callback if needed
    void _trigger_pre_parse(std::size_t remaining_args) {
        if(!pre_parse_called_) {
            pre_parse_called_ = true;
            if(pre_parse_callback_) {
                pre_parse_callback_(remaining_args);
            }
        } else if(immediate_callback_) {
            if(!name_.empty()) {
                auto pcnt = parsed_;
                auto extras = std::move(missing_);
                clear();
                parsed_ = pcnt;
                pre_parse_called_ = true;
                missing_ = std::move(extras);
            }
        }
    }

    /// Get the appropriate parent to fallthrough to which is the first one that has a name or the main app
    App *_get_fallthrough_parent() {
        if(parent_ == nullptr) {
            throw(HorribleError("No Valid parent"));
        }
        auto fallthrough_parent = parent_;
        while((fallthrough_parent->parent_ != nullptr) && (fallthrough_parent->get_name().empty())) {
            fallthrough_parent = fallthrough_parent->parent_;
        }
        return fallthrough_parent;
    }

    /// Helper function to run through all possible comparisons of subcommand names to check there is no overlap
    const std::string &_compare_subcommand_names(const App &subcom, const App &base) const {
        static const std::string estring;
        if(subcom.disabled_) {
            return estring;
        }
        for(auto &subc : base.subcommands_) {
            if(subc.get() != &subcom) {
                if(subc->disabled_) {
                    continue;
                }
                if(!subcom.get_name().empty()) {
                    if(subc->check_name(subcom.get_name())) {
                        return subcom.get_name();
                    }
                }
                if(!subc->get_name().empty()) {
                    if(subcom.check_name(subc->get_name())) {
                        return subc->get_name();
                    }
                }
                for(const auto &les : subcom.aliases_) {
                    if(subc->check_name(les)) {
                        return les;
                    }
                }
                // this loop is needed in case of ignore_underscore or ignore_case on one but not the other
                for(const auto &les : subc->aliases_) {
                    if(subcom.check_name(les)) {
                        return les;
                    }
                }
                // if the subcommand is an option group we need to check deeper
                if(subc->get_name().empty()) {
                    auto &cmpres = _compare_subcommand_names(subcom, *subc);
                    if(!cmpres.empty()) {
                        return cmpres;
                    }
                }
                // if the test subcommand is an option group we need to check deeper
                if(subcom.get_name().empty()) {
                    auto &cmpres = _compare_subcommand_names(*subc, subcom);
                    if(!cmpres.empty()) {
                        return cmpres;
                    }
                }
            }
        }
        return estring;
    }
    /// Helper function to place extra values in the most appropriate position
    void _move_to_missing(detail::Classifier val_type, const std::string &val) {
        if(allow_extras_ || subcommands_.empty()) {
            missing_.emplace_back(val_type, val);
            return;
        }
        // allow extra arguments to be places in an option group if it is allowed there
        for(auto &subc : subcommands_) {
            if(subc->name_.empty() && subc->allow_extras_) {
                subc->missing_.emplace_back(val_type, val);
                return;
            }
        }
        // if we haven't found any place to put them yet put them in missing
        missing_.emplace_back(val_type, val);
    }

  public:
    /// function that could be used by subclasses of App to shift options around into subcommands
    void _move_option(Option *opt, App *app) {
        if(opt == nullptr) {
            throw OptionNotFound("the option is NULL");
        }
        // verify that the give app is actually a subcommand
        bool found = false;
        for(auto &subc : subcommands_) {
            if(app == subc.get()) {
                found = true;
            }
        }
        if(!found) {
            throw OptionNotFound("The Given app is not a subcommand");
        }

        if((help_ptr_ == opt) || (help_all_ptr_ == opt))
            throw OptionAlreadyAdded("cannot move help options");

        if(config_ptr_ == opt)
            throw OptionAlreadyAdded("cannot move config file options");

        auto iterator =
            std::find_if(std::begin(options_), std::end(options_), [opt](const Option_p &v) { return v.get() == opt; });
        if(iterator != std::end(options_)) {
            const auto &opt_p = *iterator;
            if(std::find_if(std::begin(app->options_), std::end(app->options_), [&opt_p](const Option_p &v) {
                   return (*v == *opt_p);
               }) == std::end(app->options_)) {
                // only erase after the insertion was successful
                app->options_.push_back(std::move(*iterator));
                options_.erase(iterator);
            } else {
                throw OptionAlreadyAdded("option was not located: " + opt->get_name());
            }
        } else {
            throw OptionNotFound("could not locate the given Option");
        }
    }
};  // namespace CLI

/// Extension of App to better manage groups of options
class Option_group : public App {
  public:
    Option_group(std::string group_description, std::string group_name, App *parent)
        : App(std::move(group_description), "", parent) {
        group(group_name);
        // option groups should have automatic fallthrough
    }
    using App::add_option;
    /// Add an existing option to the Option_group
    Option *add_option(Option *opt) {
        if(get_parent() == nullptr) {
            throw OptionNotFound("Unable to locate the specified option");
        }
        get_parent()->_move_option(opt, this);
        return opt;
    }
    /// Add an existing option to the Option_group
    void add_options(Option *opt) { add_option(opt); }
    /// Add a bunch of options to the group
    template <typename... Args> void add_options(Option *opt, Args... args) {
        add_option(opt);
        add_options(args...);
    }
    using App::add_subcommand;
    /// Add an existing subcommand to be a member of an option_group
    App *add_subcommand(App *subcom) {
        App_p subc = subcom->get_parent()->get_subcommand_ptr(subcom);
        subc->get_parent()->remove_subcommand(subcom);
        add_subcommand(std::move(subc));
        return subcom;
    }
};
/// Helper function to enable one option group/subcommand when another is used
inline void TriggerOn(App *trigger_app, App *app_to_enable) {
    app_to_enable->enabled_by_default(false);
    app_to_enable->disabled_by_default();
    trigger_app->preparse_callback([app_to_enable](std::size_t) { app_to_enable->disabled(false); });
}

/// Helper function to enable one option group/subcommand when another is used
inline void TriggerOn(App *trigger_app, std::vector<App *> apps_to_enable) {
    for(auto &app : apps_to_enable) {
        app->enabled_by_default(false);
        app->disabled_by_default();
    }

    trigger_app->preparse_callback([apps_to_enable](std::size_t) {
        for(auto &app : apps_to_enable) {
            app->disabled(false);
        }
    });
}

/// Helper function to disable one option group/subcommand when another is used
inline void TriggerOff(App *trigger_app, App *app_to_enable) {
    app_to_enable->disabled_by_default(false);
    app_to_enable->enabled_by_default();
    trigger_app->preparse_callback([app_to_enable](std::size_t) { app_to_enable->disabled(); });
}

/// Helper function to disable one option group/subcommand when another is used
inline void TriggerOff(App *trigger_app, std::vector<App *> apps_to_enable) {
    for(auto &app : apps_to_enable) {
        app->disabled_by_default(false);
        app->enabled_by_default();
    }

    trigger_app->preparse_callback([apps_to_enable](std::size_t) {
        for(auto &app : apps_to_enable) {
            app->disabled();
        }
    });
}

/// Helper function to mark an option as deprecated
inline void deprecate_option(Option *opt, const std::string &replacement = "") {
    Validator deprecate_warning{[opt, replacement](std::string &) {
                                    std::cout << opt->get_name() << " is deprecated please use '" << replacement
                                              << "' instead\n";
                                    return std::string();
                                },
                                "DEPRECATED"};
    deprecate_warning.application_index(0);
    opt->check(deprecate_warning);
    if(!replacement.empty()) {
        opt->description(opt->get_description() + " DEPRECATED: please use '" + replacement + "' instead");
    }
}

/// Helper function to mark an option as deprecated
inline void deprecate_option(App *app, const std::string &option_name, const std::string &replacement = "") {
    auto opt = app->get_option(option_name);
    deprecate_option(opt, replacement);
}

/// Helper function to mark an option as deprecated
inline void deprecate_option(App &app, const std::string &option_name, const std::string &replacement = "") {
    auto opt = app.get_option(option_name);
    deprecate_option(opt, replacement);
}

/// Helper function to mark an option as retired
inline void retire_option(App *app, Option *opt) {
    App temp;
    auto option_copy = temp.add_option(opt->get_name(false, true))
                           ->type_size(opt->get_type_size_min(), opt->get_type_size_max())
                           ->expected(opt->get_expected_min(), opt->get_expected_max())
                           ->allow_extra_args(opt->get_allow_extra_args());

    app->remove_option(opt);
    auto opt2 = app->add_option(option_copy->get_name(false, true), "option has been retired and has no effect")
                    ->type_name("RETIRED")
                    ->default_str("RETIRED")
                    ->type_size(option_copy->get_type_size_min(), option_copy->get_type_size_max())
                    ->expected(option_copy->get_expected_min(), option_copy->get_expected_max())
                    ->allow_extra_args(option_copy->get_allow_extra_args());

    Validator retired_warning{[opt2](std::string &) {
                                  std::cout << "WARNING " << opt2->get_name() << " is retired and has no effect\n";
                                  return std::string();
                              },
                              ""};
    retired_warning.application_index(0);
    opt2->check(retired_warning);
}

/// Helper function to mark an option as retired
inline void retire_option(App &app, Option *opt) { retire_option(&app, opt); }

/// Helper function to mark an option as retired
inline void retire_option(App *app, const std::string &option_name) {

    auto opt = app->get_option_no_throw(option_name);
    if(opt != nullptr) {
        retire_option(app, opt);
        return;
    }
    auto opt2 = app->add_option(option_name, "option has been retired and has no effect")
                    ->type_name("RETIRED")
                    ->expected(0, 1)
                    ->default_str("RETIRED");
    Validator retired_warning{[opt2](std::string &) {
                                  std::cout << "WARNING " << opt2->get_name() << " is retired and has no effect\n";
                                  return std::string();
                              },
                              ""};
    retired_warning.application_index(0);
    opt2->check(retired_warning);
}

/// Helper function to mark an option as retired
inline void retire_option(App &app, const std::string &option_name) { retire_option(&app, option_name); }

namespace FailureMessage {

/// Printout a clean, simple message on error (the default in CLI11 1.5+)
inline std::string simple(const App *app, const Error &e) {
    std::string header = std::string(e.what()) + "\n";
    std::vector<std::string> names;

    // Collect names
    if(app->get_help_ptr() != nullptr)
        names.push_back(app->get_help_ptr()->get_name());

    if(app->get_help_all_ptr() != nullptr)
        names.push_back(app->get_help_all_ptr()->get_name());

    // If any names found, suggest those
    if(!names.empty())
        header += "Run with " + detail::join(names, " or ") + " for more information.\n";

    return header;
}

/// Printout the full help string on error (if this fn is set, the old default for CLI11)
inline std::string help(const App *app, const Error &e) {
    std::string header = std::string("ERROR: ") + e.get_name() + ": " + e.what() + "\n";
    header += app->help();
    return header;
}

}  // namespace FailureMessage

namespace detail {
/// This class is simply to allow tests access to App's protected functions
struct AppFriend {
#ifdef CLI11_CPP14

    /// Wrap _parse_short, perfectly forward arguments and return
    template <typename... Args> static decltype(auto) parse_arg(App *app, Args &&...args) {
        return app->_parse_arg(std::forward<Args>(args)...);
    }

    /// Wrap _parse_subcommand, perfectly forward arguments and return
    template <typename... Args> static decltype(auto) parse_subcommand(App *app, Args &&...args) {
        return app->_parse_subcommand(std::forward<Args>(args)...);
    }
#else
    /// Wrap _parse_short, perfectly forward arguments and return
    template <typename... Args>
    static auto parse_arg(App *app, Args &&...args) ->
        typename std::result_of<decltype (&App::_parse_arg)(App, Args...)>::type {
        return app->_parse_arg(std::forward<Args>(args)...);
    }

    /// Wrap _parse_subcommand, perfectly forward arguments and return
    template <typename... Args>
    static auto parse_subcommand(App *app, Args &&...args) ->
        typename std::result_of<decltype (&App::_parse_subcommand)(App, Args...)>::type {
        return app->_parse_subcommand(std::forward<Args>(args)...);
    }
#endif
    /// Wrap the fallthrough parent function to make sure that is working correctly
    static App *get_fallthrough_parent(App *app) { return app->_get_fallthrough_parent(); }
};
}  // namespace detail

// [CLI11:app_hpp:end]
}  // namespace CLI
