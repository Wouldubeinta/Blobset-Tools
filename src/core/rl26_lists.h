#ifndef RL26_LISTS_H
#define RL26_LISTS_H

#include <QList>
#include <QString>
#include <QStringList>

/// The RL26 Database Editor's drop down lists (rl26_lists.cpp is generated from its StringArrays.cs).
namespace Rl26Lists {

const QStringList& countriesOfBirth();   // Player country of birth (index = the stored value).
const QStringList& repCountries();       // Player representative country.
const QStringList& affiliations();       // Team affiliation.
const QStringList& stadiums();           // Stadium id - 5.
const QStringList& supporters();         // Team supporters.
const QStringList& logos();              // RL26 team logo names (ui\club_logos\large_mirrored\<name>.pc.dds).

/// Randomize Stats (Player Editor): a stat gets a random value in [min, max) for the primary roles in roleMask
/// (bit = role index) at a level (0 star, 1 average, 2 rookie).
struct RandomRange {
    int roleMask;
    int level;
    QString field;
    int min;
    int max;
};
const QList<RandomRange>& randomRanges();

/// Player rating (Rating.cs): for each primary role (0 fullback .. 8 lock) the stats that count.
struct RatingTerm {
    QString field;
    int mul;
    int div;
};
const QList<QList<RatingTerm>>& ratingTerms();

} // namespace Rl26Lists

#endif // RL26_LISTS_H
