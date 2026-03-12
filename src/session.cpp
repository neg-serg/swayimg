// SPDX-License-Identifier: MIT
// Session manager for action script integration.
// Copyright (C) 2026 Artem Senichev <artemsen@gmail.com>

#include "session.hpp"

#include "application.hpp"
#include "imagelist.hpp"
#include "log.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>

#include <unistd.h>

Session& Session::self()
{
    static Session instance;
    return instance;
}

std::filesystem::path Session::resolve_data_dir()
{
    const std::pair<const char*, const char*> env_paths[] = {
        { "XDG_DATA_HOME", "swayimg" },
        { "HOME", ".local/share/swayimg" }
    };

    for (size_t i = 0; i < sizeof(env_paths) / sizeof(env_paths[0]); ++i) {
        const char* env = env_paths[i].first;
        if (env) {
            env = std::getenv(env);
            if (!env) {
                continue;
            }
            const char* delim = strchr(env, ':');
            std::filesystem::path path;
            if (!delim) {
                path = env;
            } else {
                path = std::string(env, delim);
            }
            path /= env_paths[i].second;
            return std::filesystem::absolute(path).lexically_normal();
        }
    }

    return {};
}

std::string Session::generate_id()
{
    const auto now = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch())
                        .count();

    std::random_device rd;
    const uint16_t rnd = rd() & 0xFFFF;

    return "swayimg-" + std::to_string(getpid()) + "-" + std::to_string(ms) +
        "-" + std::to_string(rnd);
}

void Session::initialize()
{
    data_dir = resolve_data_dir();
    if (data_dir.empty()) {
        Log::warning("Session: unable to resolve data directory");
        return;
    }

    // Create data dir if needed
    std::error_code ec;
    std::filesystem::create_directories(data_dir, ec);
    if (ec) {
        Log::warning("Session: unable to create {}: {}", data_dir.string(),
                     ec.message());
        data_dir.clear();
        return;
    }

    // Clean stale files before anything else
    cleanup_stale();

    // If mode is On, activate immediately
    if (mode == Mode::On) {
        activate();
    }
    // Auto mode defers to activate_if_needed()
}

void Session::activate()
{
    if (data_dir.empty()) {
        return;
    }

    session_id = generate_id();
    filelist_path = data_dir / (session_id + ".list");
    range_path = data_dir / (session_id + ".range");
    filelist_path_str = filelist_path.string();
    range_path_str = range_path.string();
    active = true;
}

void Session::activate_if_needed()
{
    if (active || mode == Mode::Off || data_dir.empty()) {
        return;
    }

    if (mode == Mode::Auto && exec_used.load()) {
        activate();
    }
}

void Session::flush_filelist()
{
    if (!active) {
        return;
    }

    // Write file list atomically via tmp file
    const auto tmp_path =
        std::filesystem::path(filelist_path_str + ".tmp");

    try {
        std::ofstream ofs(tmp_path, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            return;
        }

        const auto all = ImageList::self().get_all();
        size_t current_index = 0;
        const auto current =
            Application::self().current_mode()->current_entry();

        for (size_t i = 0; i < all.size(); ++i) {
            ofs << all[i].path.string() << '\n';
            if (current && all[i].path == current->path) {
                current_index = i;
            }
        }
        ofs.close();

        std::filesystem::rename(tmp_path, filelist_path);

        // Write range file
        std::ofstream rofs(range_path, std::ios::out | std::ios::trunc);
        if (rofs.is_open()) {
            rofs << current_index << ':' << all.size() << '\n';
        }
    } catch (const std::exception& e) {
        Log::warning("Session: flush failed: {}", e.what());
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
    }
}

void Session::cleanup()
{
    if (!active) {
        return;
    }

    std::error_code ec;
    std::filesystem::remove(filelist_path, ec);
    std::filesystem::remove(range_path, ec);
    active = false;
}

void Session::cleanup_stale()
{
    if (data_dir.empty()) {
        return;
    }

    const auto now = std::filesystem::file_time_type::clock::now();
    const auto threshold = std::chrono::hours(24);

    try {
        for (const auto& entry :
             std::filesystem::directory_iterator(data_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const auto& name = entry.path().filename().string();
            if (!name.starts_with("swayimg-")) {
                continue;
            }
            if (!name.ends_with(".list") && !name.ends_with(".range") &&
                !name.ends_with(".from")) {
                continue;
            }

            const auto age = now - entry.last_write_time();
            if (age > threshold) {
                std::error_code ec;
                std::filesystem::remove(entry.path(), ec);
                if (!ec) {
                    Log::info("Session: removed stale {}", name);
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        Log::warning("Session: stale cleanup failed: {}", e.what());
    }
}

void Session::set_mode(Mode new_mode)
{
    mode = new_mode;
}
