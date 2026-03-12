// SPDX-License-Identifier: MIT
// Session manager for action script integration.
// Copyright (C) 2026 Artem Senichev <artemsen@gmail.com>

#pragma once

#include <atomic>
#include <filesystem>
#include <string>

/** Session manager for action script metadata. */
class Session {
public:
    /** Session management mode. */
    enum class Mode : uint8_t {
        Auto, ///< Auto-detect from exec:// keybindings
        On,   ///< Always active
        Off,  ///< Always disabled
    };

    /**
     * Get global instance of session manager.
     * @return session instance
     */
    static Session& self();

    /**
     * Initialize session: resolve data dir, generate ID.
     * Call early in Application::run().
     */
    void initialize();

    /**
     * Activate session if auto-detection criteria are met.
     * Call after Lua config finishes loading.
     */
    void activate_if_needed();

    /**
     * Flush current image list to disk (on-demand).
     * Call before fork() when spawning child processes.
     */
    void flush_filelist();

    /**
     * Clean up session files on exit.
     */
    void cleanup();

    /**
     * Remove stale session files older than 24 hours.
     */
    void cleanup_stale();

    /**
     * Set session mode.
     * @param mode session mode to set
     */
    void set_mode(Mode mode);

    /**
     * Mark that exec:// commands are used (for auto-detection).
     */
    void mark_exec_used() { exec_used = true; }

    /**
     * Check if session is active.
     * @return true if session management is active
     */
    bool is_active() const { return active; }

    /** Session environment variable values (for child processes). */
    const std::string& get_session_id() const { return session_id; }
    const std::string& get_filelist_path() const { return filelist_path_str; }
    const std::string& get_range_path() const { return range_path_str; }

private:
    /**
     * Resolve XDG data directory for session files.
     * @return resolved path, empty on failure
     */
    std::filesystem::path resolve_data_dir();

    /**
     * Generate unique session ID.
     * @return session ID string
     */
    std::string generate_id();

    /**
     * Perform activation: generate ID, set up paths.
     */
    void activate();

    Mode mode = Mode::Auto;                  ///< Session management mode
    bool active = false;                     ///< Session is active
    std::atomic<bool> exec_used { false };   ///< exec:// detected in config

    std::string session_id;                  ///< Unique session identifier
    std::filesystem::path data_dir;          ///< XDG data directory
    std::filesystem::path filelist_path;     ///< File list path
    std::filesystem::path range_path;        ///< Range file path
    std::string filelist_path_str;           ///< Cached string for env var
    std::string range_path_str;              ///< Cached string for env var
};
