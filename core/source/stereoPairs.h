#ifndef STEREO_PAIRS_H_
#define STEREO_PAIRS_H_
// Linked stereo track pairs (MK66). Per project, mono by default: the pairs are 1+2, 3+4, 5+6 and 7+8 (tracks 0-based 0+1 .. 6+7). Bit k of the
// mask links pair k. A linked pair is ONE stereo voice: the odd track (the leader, 1 3 5 7) plays, its playerEngine adopts the chain of the
// even track (the follower, 2 4 6 8) into its channel vectors (playerEngine::linkChain), chain 0 plays address[0] (L) and chain 1 address[1]
// (R). The follower track plays nothing while linked (its pattern data is kept). The mask lives in project.mtff (track.N.pair, mtff_codec.h),
// no key = no pair. Design: build/poly-t8-stereo-inv/REPORT.md section 6.
#include <stdint.h>

namespace stereopairs
{
constexpr int kPairs = 4;
inline uint8_t g_mask = 0;   // the links the engine runs with (audioEngine::setStereoPairs keeps it in step with the engine)
inline uint8_t g_linkNotice = 0;   // 1 based follower track whose notes a fresh link hides, 0 = none. The UI shows it once the sample reload popup is gone
inline bool g_linkNoticeFx = false;   // the hidden steps are not proven to hold a note (FX only steps count too): the notice says "notes or FX"

inline bool pairLinked(int pair)
{
    return pair >= 0 && pair < kPairs && ((g_mask >> pair) & 1u) != 0;
}
// track is 0 based. A follower is the even numbered track (2 4 6 8) of a linked pair, it plays nothing
inline bool isFollower(int track)
{
    return track >= 0 && track < kPairs * 2 && (track & 1) != 0 && pairLinked(track >> 1);
}
inline bool isLeader(int track)
{
    return track >= 0 && track < kPairs * 2 && (track & 1) == 0 && pairLinked(track >> 1);
}
// The track that carries a track's controls: the leader for a follower, the track itself otherwise. Mixer, Performance and the mute buttons
// act on a linked pair as one strip
inline int base(int track)
{
    return isFollower(track) ? track - 1 : track;
}
// The pattern editor draws a linked pair as ONE column labelled "1+2": the follower track is hidden (not drawn, not reachable by the cursor,
// skipped by selection edits, copy and paste), its steps stay in the pattern untouched. The next track of a selection walk
inline uint8_t next(uint8_t track)
{
    return isFollower(track + 1) ? uint8_t(track + 2) : uint8_t(track + 1);
}
// a cursor step that lands on a hidden follower goes on in direction dir
inline int8_t skip(int8_t track, int dir)
{
    return isFollower(track) ? int8_t(track + dir) : track;
}
// The n-th track that is not hidden among the 8 sample tracks (screens that lay the tracks out as 8 buttons or strips), 255 past the last one
inline uint8_t nthVisible(int n)
{
    for (int t = 0; t < kPairs * 2; ++t)
        if (!isFollower(t) && n-- == 0)
            return uint8_t(t);
    return 255;
}
// bit t = track t is hidden in the pattern editor
inline uint16_t hiddenMask()
{
    uint16_t m = 0;
    for (int k = 0; k < kPairs; ++k)
        m |= uint16_t(pairLinked(k)) << (2 * k + 1);
    return m;
}
// the label of the leader column of pair k
inline const char* label(int pair)
{
    static const char* const kNames[kPairs] = { "1+2", "3+4", "5+6", "7+8" };
    return kNames[pair & 3];
}
// Copies the leader's byte to its follower for every linked pair (mute, volume and the like keep both tracks equal, so every display agrees)
inline void mirror(uint8_t* perTrack8)
{
    for (int k = 0; k < kPairs; ++k)
        if (pairLinked(k))
            perTrack8[2 * k + 1] = perTrack8[2 * k];
}
}   // namespace stereopairs

#endif
