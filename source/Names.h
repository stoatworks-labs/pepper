#pragma once

/**
    What each bundle calls itself, in one place: the two entry files register
    these and `pgtest --names` measures them. Resolume shows sixteen bytes of
    a name and an ID is four.
*/
namespace pepper::names
{
inline constexpr const char* kMixerId   = "PG01";
inline constexpr const char* kMixerName = "SW Pepper";
inline constexpr const char* kFeedId    = "PG02";
inline constexpr const char* kFeedName  = "SW Pepper Feed";
} // namespace pepper::names
