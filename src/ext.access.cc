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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <algorithm>
#include <random>

#include "ext.access.hh"

#include <signal.h>
#include <unistd.h>

#include "base/fs_util.hh"
#include "base/lnav_log.hh"
#include "base/paths.hh"
#include "config.h"
#include "fmt/format.h"
#include "safe/safe.h"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

namespace lnav::ext {

namespace {

struct registration {
    instance_info r_info;
    std::filesystem::path r_path;
    time_t r_started{0};
};

struct registry_state {
    std::optional<registration> rs_registration;
    std::vector<std::string> rs_command_line_paths;
};

using safe_registry_state = safe::Safe<registry_state>;

safe_registry_state REGISTRY;

std::string
current_dir()
{
    std::error_code ec;
    auto retval = std::filesystem::current_path(ec);
    if (ec) {
        return "";
    }
    return retval.string();
}

void
prune_dead_instances(const std::filesystem::path& dir)
{
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const auto& path = entry.path();
        if (path.extension() != ".json") {
            continue;
        }

        auto pid_str = path.stem().string();
        char* end = nullptr;
        auto pid = strtol(pid_str.c_str(), &end, 10);
        if (end == pid_str.c_str() || *end != '\0' || pid <= 0
            || pid == getpid())
        {
            continue;
        }
        if (kill(pid, 0) == -1 && errno == ESRCH) {
            log_info("removing discovery file for dead instance: %s",
                     path.c_str());
            std::filesystem::remove(path, ec);
        }
    }
}

void
write_registration(const registration& reg,
                   const std::vector<std::string>& paths)
{
    auto rec = instance_record{
        getpid(),
        PACKAGE_VERSION,
        reg.r_info.ii_name,
        current_dir(),
        paths,
        reg.r_info.ii_url,
        reg.r_info.ii_port,
        reg.r_info.ii_api_key,
        reg.r_started,
    };
    auto content = instance_record::handlers.to_string(rec);

    auto write_res = lnav::filesystem::write_file(
        reg.r_path, string_fragment::from_str(content));
    if (write_res.isErr()) {
        log_error("unable to write discovery file: %s -- %s",
                  reg.r_path.c_str(),
                  write_res.unwrapErr().c_str());
    }
}

}  // namespace

const string_fragment instance_record::SCHEMA_ID
    = "https://lnav.org/external-access-instance-v1.schema.json"_frag;

static int
read_schema_id(yajlpp_parse_context* ypc,
               const unsigned char* str,
               size_t len,
               yajl_string_props_t*)
{
    return 1;
}

const typed_json_path_container<instance_record> instance_record::handlers = typed_json_path_container<instance_record>{
    json_path_handler("$schema", read_schema_id)
        .with_synopsis("<schema-uri>")
        .with_description("The URI that specifies the schema that describes this type of file")
        .with_example(SCHEMA_ID),
    yajlpp::property_handler("pid")
        .with_description("The ID of the lnav process.  Clients should ignore the file if this process is not running.")
        .for_field(&instance_record::ir_pid),
    yajlpp::property_handler("version")
        .with_description("The version of lnav")
        .for_field(&instance_record::ir_version),
    yajlpp::property_handler("name")
        .with_description("The name of the instance, set with the --name option of the :external-access command.  Defaults to the name of the directory lnav was started in.")
        .for_field(&instance_record::ir_name),
    yajlpp::property_handler("cwd")
        .with_description("The current working directory of lnav, which is updated by the :cd command")
        .for_field(&instance_record::ir_cwd),
    yajlpp::property_handler("paths#")
        .with_description("The paths given on the command-line.  Local paths and glob patterns are absolute; remote paths and URLs are as given.")
        .for_field(&instance_record::ir_paths),
    yajlpp::property_handler("url")
        .with_description("The base URL of the external-access server")
        .for_field(&instance_record::ir_url),
    yajlpp::property_handler("port")
        .with_description("The port number of the external-access server")
        .for_field(&instance_record::ir_port),
    yajlpp::property_handler("api_key")
        .with_description("The API key, before Base64 encoding.  Send it Base64-encoded in the X-Api-Key header.")
        .for_field(&instance_record::ir_api_key),
    yajlpp::property_handler("started")
        .with_description("When the server was started, in seconds since the epoch")
        .for_field(&instance_record::ir_started),
}
    .with_schema_id2(SCHEMA_ID)
    .with_description2("Written by each running lnav whose external-access server is open, so clients can find and connect to it.");

void
set_command_line_paths(std::vector<std::string> paths)
{
    safe::WriteAccess<safe_registry_state> rs(REGISTRY);

    rs->rs_command_line_paths = std::move(paths);
}

std::filesystem::path
instance_dir()
{
    return lnav::paths::dotlnav() / "external-access";
}

std::string
generate_api_key()
{
    std::random_device rd;
    std::string retval;

    for (int lpc = 0; lpc < 4; lpc++) {
        fmt::format_to(std::back_inserter(retval), FMT_STRING("{:08x}"), rd());
    }

    return retval;
}

void
register_instance(instance_info ii)
{
    safe::WriteAccess<safe_registry_state> rs(REGISTRY);

    auto dir = instance_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        log_error("unable to create discovery directory: %s -- %s",
                  dir.c_str(),
                  ec.message().c_str());
    }
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all, ec);
    prune_dead_instances(dir);

    if (rs->rs_registration) {
        rs->rs_registration->r_info = std::move(ii);
    } else {
        rs->rs_registration = registration{
            std::move(ii),
            dir / fmt::format(FMT_STRING("{}.json"), getpid()),
            time(nullptr),
        };
    }
    write_registration(rs->rs_registration.value(), rs->rs_command_line_paths);
}

void
refresh_instance()
{
    safe::ReadAccess<safe_registry_state> rs(REGISTRY);

    if (rs->rs_registration) {
        write_registration(rs->rs_registration.value(),
                           rs->rs_command_line_paths);
    }
}

void
unregister_instance()
{
    safe::WriteAccess<safe_registry_state> rs(REGISTRY);

    if (rs->rs_registration) {
        std::error_code ec;
        std::filesystem::remove(rs->rs_registration->r_path, ec);
        rs->rs_registration = std::nullopt;
    }
}

std::vector<instance_record>
running_instances()
{
    static const intern_string_t SRC
        = intern_string::lookup("external-access");

    std::vector<instance_record> retval;
    std::error_code ec;

    for (const auto& entry :
         std::filesystem::directory_iterator(instance_dir(), ec))
    {
        const auto& path = entry.path();
        if (path.extension() != ".json") {
            continue;
        }

        auto read_res = lnav::filesystem::read_file(path);
        if (read_res.isErr()) {
            log_warning("unable to read discovery file: %s -- %s",
                        path.c_str(),
                        read_res.unwrapErr().c_str());
            continue;
        }
        auto content = read_res.unwrap();
        auto parse_res = instance_record::handlers.parser_for(SRC).of(
            string_fragment::from_str(content));
        if (parse_res.isErr()) {
            log_warning("unable to parse discovery file: %s -- %s",
                        path.c_str(),
                        parse_res.unwrapErr()[0]
                            .to_attr_line()
                            .get_string()
                            .c_str());
            continue;
        }

        auto rec = parse_res.unwrap();
        if (rec.ir_pid <= 0 || rec.ir_pid == getpid()
            || (kill(rec.ir_pid, 0) == -1 && errno == ESRCH))
        {
            continue;
        }
        retval.emplace_back(std::move(rec));
    }

    std::stable_sort(retval.begin(),
                     retval.end(),
                     [](const auto& lhs, const auto& rhs) {
                         return lhs.ir_started < rhs.ir_started;
                     });

    return retval;
}

std::optional<instance_info>
current_instance()
{
    safe::ReadAccess<safe_registry_state> rs(REGISTRY);

    if (rs->rs_registration) {
        return rs->rs_registration->r_info;
    }
    return std::nullopt;
}

}  // namespace lnav::ext
