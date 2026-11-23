#pragma once
// SVG icons used by the board (drawn by PulseUI itself, tinted with the text color).
namespace icons {

inline constexpr const char* logo =
    R"svg(<svg viewBox="0 0 24 24"><rect x="2" y="4" width="20" height="16" rx="3" fill="#6E8BFF"/><circle cx="8.5" cy="9.5" r="2" fill="#FFFFFF"/><path d="M4 18l5-5 3 3 4-5 4 7z" fill="#FFFFFF" fill-opacity="0.85"/></svg>)svg";

inline constexpr const char* open =
    R"svg(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 19V6a2 2 0 0 1 2-2h4l2 2h6a2 2 0 0 1 2 2v2"/><path d="M3 19l3-8h16l-3 8z"/></svg>)svg";

inline constexpr const char* moon =
    R"svg(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></svg>)svg";

inline constexpr const char* sparkle =
    R"svg(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 3v4M12 17v4M3 12h4M17 12h4M6 6l2.5 2.5M15.5 15.5L18 18M6 18l2.5-2.5M15.5 8.5L18 6"/></svg>)svg";

inline constexpr const char* trash =
    R"svg(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6m3 0V4a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v2"/></svg>)svg";

inline constexpr const char* drop =
    R"svg(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6"><path d="M12 3v12M7 10l5 5 5-5"/><path d="M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/></svg>)svg";

// decorations of the generated cards
inline constexpr const char* mountain =
    R"svg(<svg viewBox="0 0 24 24"><path d="M2 20l7-11 4 6 3-4 6 9z" fill="#FFFFFF"/><circle cx="17" cy="6" r="2.5" fill="#FFFFFF"/></svg>)svg";
inline constexpr const char* wave =
    R"svg(<svg viewBox="0 0 24 24" fill="none" stroke="#FFFFFF" stroke-width="2"><path d="M2 9c3-3 5 3 8 0s5 3 8 0 3-2 4-1"/><path d="M2 15c3-3 5 3 8 0s5 3 8 0 3-2 4-1"/></svg>)svg";
inline constexpr const char* leaf =
    R"svg(<svg viewBox="0 0 24 24"><path d="M5 19C5 9 11 4 20 4c0 9-5 15-15 15z" fill="#FFFFFF"/><path d="M5 19l9-9" stroke="#000000" stroke-opacity="0.25" stroke-width="1.5"/></svg>)svg";
inline constexpr const char* star =
    R"svg(<svg viewBox="0 0 24 24"><path fill-rule="evenodd" fill="#FFFFFF" d="M12 2l3 7h7l-5.5 4.5 2 7.5-6.5-4.5L5.5 21l2-7.5L2 9h7z M12 8l-1.2 3h-3l2.4 2-0.8 3 2.6-1.8 2.6 1.8-0.8-3 2.4-2h-3z"/></svg>)svg";

} // namespace icons
