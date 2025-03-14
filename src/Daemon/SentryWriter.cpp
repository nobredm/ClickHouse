#include <Daemon/SentryWriter.h>

#include <Poco/Util/Application.h>
#include <Poco/Util/LayeredConfiguration.h>

#include <base/defines.h>
#include <base/getFQDNOrHostName.h>
#include <base/getMemoryAmount.h>
#include <Common/logger_useful.h>

#include <Common/formatReadable.h>
#include <Common/SymbolIndex.h>
#include <Common/StackTrace.h>
#include <Common/getNumberOfPhysicalCPUCores.h>
#include <Core/ServerUUID.h>
#include <Common/hex.h>

#include "Common/config_version.h"
#include <Common/config.h>

#if USE_SENTRY && !defined(KEEPER_STANDALONE_BUILD)

#    include <sentry.h>
#    include <cstdio>
#    include <filesystem>

namespace fs = std::filesystem;

namespace
{

bool initialized = false;
bool anonymize = false;
std::string server_data_path;

void setExtras()
{
    if (!anonymize)
        sentry_set_extra("server_name", sentry_value_new_string(getFQDNOrHostName().c_str()));

    DB::UUID server_uuid = DB::ServerUUID::get();
    if (server_uuid != DB::UUIDHelpers::Nil)
    {
        std::string server_uuid_str = DB::toString(server_uuid);
        sentry_set_extra("server_uuid", sentry_value_new_string(server_uuid_str.c_str()));
    }

    sentry_set_tag("version", VERSION_STRING);
    sentry_set_extra("version_githash", sentry_value_new_string(VERSION_GITHASH));
    sentry_set_extra("version_describe", sentry_value_new_string(VERSION_DESCRIBE));
    sentry_set_extra("version_integer", sentry_value_new_int32(VERSION_INTEGER));
    sentry_set_extra("version_revision", sentry_value_new_int32(VERSION_REVISION));
    sentry_set_extra("version_major", sentry_value_new_int32(VERSION_MAJOR));
    sentry_set_extra("version_minor", sentry_value_new_int32(VERSION_MINOR));
    sentry_set_extra("version_patch", sentry_value_new_int32(VERSION_PATCH));
    sentry_set_extra("version_official", sentry_value_new_string(VERSION_OFFICIAL));

    /// Sentry does not support 64-bit integers.
    sentry_set_extra("total_ram", sentry_value_new_string(formatReadableSizeWithBinarySuffix(getMemoryAmountOrZero()).c_str()));
    sentry_set_extra("physical_cpu_cores", sentry_value_new_int32(getNumberOfPhysicalCPUCores()));

    if (!server_data_path.empty())
        sentry_set_extra("disk_free_space", sentry_value_new_string(formatReadableSizeWithBinarySuffix(fs::space(server_data_path).free).c_str()));
}

}


void SentryWriter::initialize(Poco::Util::LayeredConfiguration & config)
{
    bool enabled = false;
    bool debug = config.getBool("send_crash_reports.debug", false);
    auto * logger = &Poco::Logger::get("SentryWriter");

    if (config.getBool("send_crash_reports.enabled", false))
    {
        if (debug || (strlen(VERSION_OFFICIAL) > 0)) //-V560
            enabled = true;
    }

    if (enabled)
    {
        server_data_path = config.getString("path", "");
        const std::filesystem::path & default_tmp_path = fs::path(config.getString("tmp_path", fs::temp_directory_path())) / "sentry";
        const std::string & endpoint
            = config.getString("send_crash_reports.endpoint");
        const std::string & temp_folder_path
            = config.getString("send_crash_reports.tmp_path", default_tmp_path);
        fs::create_directories(temp_folder_path);

        sentry_options_t * options = sentry_options_new();  /// will be freed by sentry_init or sentry_shutdown
        sentry_options_set_release(options, VERSION_STRING_SHORT);
        if (debug)
        {
            sentry_options_set_debug(options, 1);
        }
        sentry_options_set_dsn(options, endpoint.c_str());
        sentry_options_set_database_path(options, temp_folder_path.c_str());
        if (strstr(VERSION_DESCRIBE, "-stable") || strstr(VERSION_DESCRIBE, "-lts"))
        {
            sentry_options_set_environment(options, "prod");
        }
        else
        {
            sentry_options_set_environment(options, "test");
        }

        const std::string & http_proxy = config.getString("send_crash_reports.http_proxy", "");
        if (!http_proxy.empty())
        {
            sentry_options_set_http_proxy(options, http_proxy.c_str());
        }

        int init_status = sentry_init(options);
        if (!init_status)
        {
            initialized = true;
            anonymize = config.getBool("send_crash_reports.anonymize", false);
            LOG_INFO(
                logger,
                "Sending crash reports is initialized with {} endpoint and {} temp folder{}",
                endpoint,
                temp_folder_path,
                anonymize ? " (anonymized)" : "");
        }
        else
        {
            LOG_WARNING(logger, "Sending crash reports failed to initialize with {} status", init_status);
        }
    }
    else
    {
        LOG_INFO(logger, "Sending crash reports is disabled");
    }
}

void SentryWriter::shutdown()
{
    if (initialized)
        sentry_shutdown();
}

void SentryWriter::onFault(int sig, const std::string & error_message, const StackTrace & stack_trace)
{
    auto * logger = &Poco::Logger::get("SentryWriter");
    if (initialized)
    {
        sentry_value_t event = sentry_value_new_message_event(SENTRY_LEVEL_FATAL, "fault", error_message.c_str());
        sentry_set_tag("signal", strsignal(sig));
        sentry_set_extra("signal_number", sentry_value_new_int32(sig));

        #if defined(__ELF__) && !defined(__FreeBSD__)
            const String & build_id_hex = DB::SymbolIndex::instance()->getBuildIDHex();
            sentry_set_tag("build_id", build_id_hex.c_str());
        #endif

        setExtras();

        /// Prepare data for https://develop.sentry.dev/sdk/event-payloads/stacktrace/
        sentry_value_t sentry_frames = sentry_value_new_list();
        size_t stack_size = stack_trace.getSize();
        if (stack_size > 0)
        {
            ssize_t offset = stack_trace.getOffset();

            char instruction_addr[19]
            {
                '0', 'x',
                '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
                '\0'
            };

            StackTrace::Frames frames;
            StackTrace::symbolize(stack_trace.getFramePointers(), offset, stack_size, frames);

            for (ssize_t i = stack_size - 1; i >= offset; --i)
            {
                const StackTrace::Frame & current_frame = frames[i];
                sentry_value_t sentry_frame = sentry_value_new_object();
                UInt64 frame_ptr = reinterpret_cast<UInt64>(current_frame.virtual_addr);

                writeHexUIntLowercase(frame_ptr, instruction_addr + 2);
                sentry_value_set_by_key(sentry_frame, "instruction_addr", sentry_value_new_string(instruction_addr));

                if (current_frame.symbol.has_value())
                    sentry_value_set_by_key(sentry_frame, "function", sentry_value_new_string(current_frame.symbol.value().c_str()));

                if (current_frame.file.has_value())
                    sentry_value_set_by_key(sentry_frame, "filename", sentry_value_new_string(current_frame.file.value().c_str()));

                if (current_frame.line.has_value())
                    sentry_value_set_by_key(sentry_frame, "lineno", sentry_value_new_int32(current_frame.line.value()));

                sentry_value_append(sentry_frames, sentry_frame);
            }
        }

        /// Prepare data for https://develop.sentry.dev/sdk/event-payloads/threads/
        /// Stacktrace is filled only for a single thread that failed
        sentry_value_t stacktrace = sentry_value_new_object();
        sentry_value_set_by_key(stacktrace, "frames", sentry_frames);

        sentry_value_t thread = sentry_value_new_object();
        sentry_value_set_by_key(thread, "stacktrace", stacktrace);

        sentry_value_t values = sentry_value_new_list();
        sentry_value_append(values, thread);

        sentry_value_t threads = sentry_value_new_object();
        sentry_value_set_by_key(threads, "values", values);

        sentry_value_set_by_key(event, "threads", threads);

        LOG_INFO(logger, "Sending crash report");
        sentry_capture_event(event);
        shutdown();
    }
    else
    {
        LOG_INFO(logger, "Not sending crash report");
    }
}

#else

void SentryWriter::initialize(Poco::Util::LayeredConfiguration &) {}
void SentryWriter::shutdown() {}
void SentryWriter::onFault(int, const std::string &, const StackTrace &) {}

#endif
