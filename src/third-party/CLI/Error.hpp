// Copyright (c) 2017-2022, University of Cincinnati, developed by Henry Schreiner
// under NSF AWARD 1414736 and by the respective contributors.
// All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// [CLI11:public_includes:set]
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
// [CLI11:public_includes:end]

// CLI library includes
#include "StringTools.hpp"

namespace CLI {
// [CLI11:error_hpp:verbatim]

// Use one of these on all error classes.
// These are temporary and are undef'd at the end of this file.
#define CLI11_ERROR_DEF(parent, name)                                                                                  \
  protected:                                                                                                           \
    name(std::string ename, std::string msg, int exit_code) : parent(std::move(ename), std::move(msg), exit_code) {}   \
    name(std::string ename, std::string msg, ExitCodes exit_code)                                                      \
        : parent(std::move(ename), std::move(msg), exit_code) {}                                                       \
                                                                                                                       \
  public:                                                                                                              \
    name(std::string msg, ExitCodes exit_code) : parent(#name, std::move(msg), exit_code) {}                           \
    name(std::string msg, int exit_code) : parent(#name, std::move(msg), exit_code) {}

// This is added after the one above if a class is used directly and builds its own message
#define CLI11_ERROR_SIMPLE(name)                                                                                       \
    explicit name(std::string msg) : name(#name, msg, ExitCodes::name) {}

/// These codes are part of every error in CLI. They can be obtained from e using e.exit_code or as a quick shortcut,
/// int values from e.get_error_code().
enum class ExitCodes {
    Success = 0,
    IncorrectConstruction = 100,
    BadNameString,
    OptionAlreadyAdded,
    FileError,
    ConversionError,
    ValidationError,
    RequiredError,
    RequiresError,
    ExcludesError,
    ExtrasError,
    ConfigError,
    InvalidError,
    HorribleError,
    OptionNotFound,
    ArgumentMismatch,
    BaseClass = 127
};

// Error definitions

/// @defgroup error_group Errors
/// @brief Errors thrown by CLI11
///
/// These are the errors that can be thrown. Some of them, like CLI::Success, are not really errors.
/// @{

/// All errors derive from this one
class Error : public std::runtime_error {
    int actual_exit_code;
    std::string error_name{"Error"};

  public:
    int get_exit_code() const { return actual_exit_code; }

    std::string get_name() const { return error_name; }

    Error(std::string name, std::string msg, int exit_code = static_cast<int>(ExitCodes::BaseClass))
        : runtime_error(msg), actual_exit_code(exit_code), error_name(std::move(name)) {}

    Error(std::string name, std::string msg, ExitCodes exit_code) : Error(name, msg, static_cast<int>(exit_code)) {}
};

// Note: Using Error::Error constructors does not work on GCC 4.7

/// Construction errors (not in parsing)
class ConstructionError : public Error {
    CLI11_ERROR_DEF(Error, ConstructionError)
};

/// Thrown when an option is set to conflicting values (non-vector and multi args, for example)
class IncorrectConstruction : public ConstructionError {
    CLI11_ERROR_DEF(ConstructionError, IncorrectConstruction)
    CLI11_ERROR_SIMPLE(IncorrectConstruction)
    static IncorrectConstruction PositionalFlag(std::string name) {
        return IncorrectConstruction(name + ": Flags cannot be positional");
    }
    static IncorrectConstruction Set0Opt(std::string name) {
        return IncorrectConstruction(name + ": Cannot set 0 expected, use a flag instead");
    }
    static IncorrectConstruction SetFlag(std::string name) {
        return IncorrectConstruction(name + ": Cannot set an expected number for flags");
    }
    static IncorrectConstruction ChangeNotVector(std::string name) {
        return IncorrectConstruction(name + ": You can only change the expected arguments for vectors");
    }
    static IncorrectConstruction AfterMultiOpt(std::string name) {
        return IncorrectConstruction(
            name + ": You can't change expected arguments after you've changed the multi option policy!");
    }
    static IncorrectConstruction MissingOption(std::string name) {
        return IncorrectConstruction("Option " + name + " is not defined");
    }
    static IncorrectConstruction MultiOptionPolicy(std::string name) {
        return IncorrectConstruction(name + ": multi_option_policy only works for flags and exact value options");
    }
};

/// Thrown on construction of a bad name
class BadNameString : public ConstructionError {
    CLI11_ERROR_DEF(ConstructionError, BadNameString)
    CLI11_ERROR_SIMPLE(BadNameString)
    static BadNameString OneCharName(std::string name) { return BadNameString("Invalid one char name: " + name); }
    static BadNameString BadLongName(std::string name) { return BadNameString("Bad long name: " + name); }
    static BadNameString DashesOnly(std::string name) {
        return BadNameString("Must have a name, not just dashes: " + name);
    }
    static BadNameString MultiPositionalNames(std::string name) {
        return BadNameString("Only one positional name allowed, remove: " + name);
    }
};

/// Thrown when an option already exists
class OptionAlreadyAdded : public ConstructionError {
    CLI11_ERROR_DEF(ConstructionError, OptionAlreadyAdded)
    explicit OptionAlreadyAdded(std::string name)
        : OptionAlreadyAdded(name + " is already added", ExitCodes::OptionAlreadyAdded) {}
    static OptionAlreadyAdded Requires(std::string name, std::string other) {
        return OptionAlreadyAdded(name + " requires " + other, ExitCodes::OptionAlreadyAdded);
    }
    static OptionAlreadyAdded Excludes(std::string name, std::string other) {
        return OptionAlreadyAdded(name + " excludes " + other, ExitCodes::OptionAlreadyAdded);
    }
};

// Parsing errors

/// Anything that can error in Parse
class ParseError : public Error {
    CLI11_ERROR_DEF(Error, ParseError)
};

// Not really "errors"

/// This is a successful completion on parsing, supposed to exit
class Success : public ParseError {
    CLI11_ERROR_DEF(ParseError, Success)
    Success() : Success("Successfully completed, should be caught and quit", ExitCodes::Success) {}
};

/// -h or --help on command line
class CallForHelp : public Success {
    CLI11_ERROR_DEF(Success, CallForHelp)
    CallForHelp() : CallForHelp("This should be caught in your main function, see examples", ExitCodes::Success) {}
};

/// Usually something like --help-all on command line
class CallForAllHelp : public Success {
    CLI11_ERROR_DEF(Success, CallForAllHelp)
    CallForAllHelp()
        : CallForAllHelp("This should be caught in your main function, see examples", ExitCodes::Success) {}
};

/// -v or --version on command line
class CallForVersion : public Success {
    CLI11_ERROR_DEF(Success, CallForVersion)
    CallForVersion()
        : CallForVersion("This should be caught in your main function, see examples", ExitCodes::Success) {}
};

/// Does not output a diagnostic in CLI11_PARSE, but allows main() to return with a specific error code.
class RuntimeError : public ParseError {
    CLI11_ERROR_DEF(ParseError, RuntimeError)
    explicit RuntimeError(int exit_code = 1) : RuntimeError("Runtime error", exit_code) {}
};

/// Thrown when parsing an INI file and it is missing
class FileError : public ParseError {
    CLI11_ERROR_DEF(ParseError, FileError)
    CLI11_ERROR_SIMPLE(FileError)
    static FileError Missing(std::string name) { return FileError(name + " was not readable (missing?)"); }
};

/// Thrown when conversion call back fails, such as when an int fails to coerce to a string
class ConversionError : public ParseError {
    CLI11_ERROR_DEF(ParseError, ConversionError)
    CLI11_ERROR_SIMPLE(ConversionError)
    ConversionError(std::string member, std::string name)
        : ConversionError("The value " + member + " is not an allowed value for " + name) {}
    ConversionError(std::string name, std::vector<std::string> results)
        : ConversionError("Could not convert: " + name + " = " + detail::join(results)) {}
    static ConversionError TooManyInputsFlag(std::string name) {
        return ConversionError(name + ": too many inputs for a flag");
    }
    static ConversionError TrueFalse(std::string name) {
        return ConversionError(name + ": Should be true/false or a number");
    }
};

/// Thrown when validation of results fails
class ValidationError : public ParseError {
    CLI11_ERROR_DEF(ParseError, ValidationError)
    CLI11_ERROR_SIMPLE(ValidationError)
    explicit ValidationError(std::string name, std::string msg) : ValidationError(name + ": " + msg) {}
};

/// Thrown when a required option is missing
class RequiredError : public ParseError {
    CLI11_ERROR_DEF(ParseError, RequiredError)
    explicit RequiredError(std::string name) : RequiredError(name + " is required", ExitCodes::RequiredError) {}
    static RequiredError Subcommand(std::size_t min_subcom) {
        if(min_subcom == 1) {
            return RequiredError("A subcommand");
        }
        return RequiredError("Requires at least " + std::to_string(min_subcom) + " subcommands",
                             ExitCodes::RequiredError);
    }
    static RequiredError
    Option(std::size_t min_option, std::size_t max_option, std::size_t used, const std::string &option_list) {
        if((min_option == 1) && (max_option == 1) && (used == 0))
            return RequiredError("Exactly 1 option from [" + option_list + "]");
        if((min_option == 1) && (max_option == 1) && (used > 1)) {
            return RequiredError("Exactly 1 option from [" + option_list + "] is required and " + std::to_string(used) +
                                     " were given",
                                 ExitCodes::RequiredError);
        }
        if((min_option == 1) && (used == 0))
            return RequiredError("At least 1 option from [" + option_list + "]");
        if(used < min_option) {
            return RequiredError("Requires at least " + std::to_string(min_option) + " options used and only " +
                                     std::to_string(used) + "were given from [" + option_list + "]",
                                 ExitCodes::RequiredError);
        }
        if(max_option == 1)
            return RequiredError("Requires at most 1 options be given from [" + option_list + "]",
                                 ExitCodes::RequiredError);

        return RequiredError("Requires at most " + std::to_string(max_option) + " options be used and " +
                                 std::to_string(used) + "were given from [" + option_list + "]",
                             ExitCodes::RequiredError);
    }
};

/// Thrown when the wrong number of arguments has been received
class ArgumentMismatch : public ParseError {
    CLI11_ERROR_DEF(ParseError, ArgumentMismatch)
    CLI11_ERROR_SIMPLE(ArgumentMismatch)
    ArgumentMismatch(std::string name, int expected, std::size_t received)
        : ArgumentMismatch(expected > 0 ? ("Expected exactly " + std::to_string(expected) + " arguments to " + name +
                                           ", got " + std::to_string(received))
                                        : ("Expected at least " + std::to_string(-expected) + " arguments to " + name +
                                           ", got " + std::to_string(received)),
                           ExitCodes::ArgumentMismatch) {}

    static ArgumentMismatch AtLeast(std::string name, int num, std::size_t received) {
        return ArgumentMismatch(name + ": At least " + std::to_string(num) + " required but received " +
                                std::to_string(received));
    }
    static ArgumentMismatch AtMost(std::string name, int num, std::size_t received) {
        return ArgumentMismatch(name + ": At Most " + std::to_string(num) + " required but received " +
                                std::to_string(received));
    }
    static ArgumentMismatch TypedAtLeast(std::string name, int num, std::string type) {
        return ArgumentMismatch(name + ": " + std::to_string(num) + " required " + type + " missing");
    }
    static ArgumentMismatch FlagOverride(std::string name) {
        return ArgumentMismatch(name + " was given a disallowed flag override");
    }
    static ArgumentMismatch PartialType(std::string name, int num, std::string type) {
        return ArgumentMismatch(name + ": " + type + " only partially specified: " + std::to_string(num) +
                                " required for each element");
    }
};

/// Thrown when a requires option is missing
class RequiresError : public ParseError {
    CLI11_ERROR_DEF(ParseError, RequiresError)
    RequiresError(std::string curname, std::string subname)
        : RequiresError(curname + " requires " + subname, ExitCodes::RequiresError) {}
};

/// Thrown when an excludes option is present
class ExcludesError : public ParseError {
    CLI11_ERROR_DEF(ParseError, ExcludesError)
    ExcludesError(std::string curname, std::string subname)
        : ExcludesError(curname + " excludes " + subname, ExitCodes::ExcludesError) {}
};

/// Thrown when too many positionals or options are found
class ExtrasError : public ParseError {
    CLI11_ERROR_DEF(ParseError, ExtrasError)
    explicit ExtrasError(std::vector<std::string> args)
        : ExtrasError((args.size() > 1 ? "The following arguments were not expected: "
                                       : "The following argument was not expected: ") +
                          detail::rjoin(args, " "),
                      ExitCodes::ExtrasError) {}
    ExtrasError(const std::string &name, std::vector<std::string> args)
        : ExtrasError(name,
                      (args.size() > 1 ? "The following arguments were not expected: "
                                       : "The following argument was not expected: ") +
                          detail::rjoin(args, " "),
                      ExitCodes::ExtrasError) {}
};

/// Thrown when extra values are found in an INI file
class ConfigError : public ParseError {
    CLI11_ERROR_DEF(ParseError, ConfigError)
    CLI11_ERROR_SIMPLE(ConfigError)
    static ConfigError Extras(std::string item) { return ConfigError("INI was not able to parse " + item); }
    static ConfigError NotConfigurable(std::string item) {
        return ConfigError(item + ": This option is not allowed in a configuration file");
    }
};

/// Thrown when validation fails before parsing
class InvalidError : public ParseError {
    CLI11_ERROR_DEF(ParseError, InvalidError)
    explicit InvalidError(std::string name)
        : InvalidError(name + ": Too many positional arguments with unlimited expected args", ExitCodes::InvalidError) {
    }
};

/// This is just a safety check to verify selection and parsing match - you should not ever see it
/// Strings are directly added to this error, but again, it should never be seen.
class HorribleError : public ParseError {
    CLI11_ERROR_DEF(ParseError, HorribleError)
    CLI11_ERROR_SIMPLE(HorribleError)
};

// After parsing

/// Thrown when counting a non-existent option
class OptionNotFound : public Error {
    CLI11_ERROR_DEF(Error, OptionNotFound)
    explicit OptionNotFound(std::string name) : OptionNotFound(name + " not found", ExitCodes::OptionNotFound) {}
};

#undef CLI11_ERROR_DEF
#undef CLI11_ERROR_SIMPLE

/// @}

// [CLI11:error_hpp:end]
}  // namespace CLI
