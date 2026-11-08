#ifndef RLL3_SAVE_LISTS_H
#define RLL3_SAVE_LISTS_H

#include <QList>
#include <QStringList>

// Rugby League Live 3 Save Editor lists (from Wouldy's RLL3 Save Editor), the rest are Rll3Lists (the Database
// Editor's: countries, supporters, emblems, jerseys).
namespace Rll3SaveLists {

struct Choice {
    const char* name;   // UTF-8
    int value;
};

const QList<Choice>& stadiums();            // Stadium id (u8), in the editor's order.
const QList<Choice>& teamCommentary();      // Team commentary id (u16).
const QStringList& commentarySurnames();    // Player commentary surname (u16 position; the last, "No Commentary", is 65535).
const QList<unsigned int>& jerseyPalette(); // The game's colour palette for jerseys and kit (0xRRGGBB by stored index).
const QList<unsigned int>& teamPalette();   // Team colour sets.
const QList<unsigned int>& skinPalette();   // Skin tones.

} // namespace Rll3SaveLists

#endif // RLL3_SAVE_LISTS_H
