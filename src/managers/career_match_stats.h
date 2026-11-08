#ifndef CAREER_MATCH_STATS_H
#define CAREER_MATCH_STATS_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

// ==========================================
// Career match statistics (the C# career editors' Match Statistics / Player Statistics windows)
// ==========================================
// Rll4CareerEditorManager and Rll3CareerEditorManager read a fixture's team and player stats into these (their
// layouts differ), and build() makes what SaveEditor.qml's match view shows: the team v team stats, both teams'
// players with their match ratings, and the man of the match.
namespace CareerMatch {

// The stat names (the C# MatchesResultsData field names).
//   Player: ballStrips, bonusTries (RLL4), conversions, conversionAttempts, fends, fieldGoalsAttempts, fieldGoals,
//           fortyTwenties, handlingErrors, hitups, interceptedPasses, interceptions, kickMetresGained, kicksInPlay,
//           knockOns, lineBreakAssists, lineBreaks, allRunMetres, minutesPlayed, missedTackles, offloads,
//           oneOnOneTackles, passes, penaltiesConceded, penaltyGoals, penaltyGoalAttempts, allRuns, tackleBreaks,
//           tackles, tries, tryAssists.
//   Team:   completionAttempts, errors, fieldgoal, goals, lineBreaks, tackles, missTackles, timeInPossession, tries,
//           completions.
struct Player {
    int id = 0;
    int position = 0;   // In the match (0 = fullback ...; the file keeps the halfback at 5, the five-eighth at 6).
    QString name;
    QString role;
    int playerIndex = -1;
    QHash<QString, int> s;
};

struct Team {
    int id = 0;
    int teamIndex = -1;
    QString name;
    QString location;   // The name in two parts (the C# editor's match view): "Cronulla-Sutherland" / "Sharks".
    QString club;
    QString logoUrl;
    QString abbreviation;
    QString colour;
    int score = 0;
    QHash<QString, int> t;
    QList<Player> players;
};

// The C# editor's match rating (PlayerMatchRating): the stats weighted, plus twice the winning margin (at most 30)
// for the winners. RLL3 counts ball strips (x2) where RLL4 counts bonus tries (x17).
inline double rating(const Player& p, int margin, bool rll3) {
    const auto v = [&p](const char* k) { return double(p.s.value(QString::fromLatin1(k))); };
    double r = (rll3 ? v("ballStrips") * 2 : v("bonusTries") * 17) + v("conversions") * 2
             + (v("conversions") - v("conversionAttempts")) * -2 + v("fends") + v("fieldGoals") * 5
             + (v("fieldGoals") - v("fieldGoalsAttempts")) * -1 + v("fortyTwenties") * 2 + v("hitups") * 2
             + v("interceptedPasses") * 2 + v("interceptions") * 2 + v("kickMetresGained") * 0.05 + v("kicksInPlay")
             + v("lineBreakAssists") * 2 + v("lineBreaks") * 4 + v("knockOns") * -10 + v("allRunMetres") * 0.1
             + v("minutesPlayed") * 0.1 + v("missedTackles") * -1 + v("offloads") * 2 + v("oneOnOneTackles") * 3
             + v("passes") * 0.2 + v("penaltiesConceded") * -2 + v("penaltyGoals") * 10
             + (v("penaltyGoals") - v("penaltiesConceded")) * -2 + v("allRuns") * 0.5 + v("tackleBreaks") * 3
             + v("tackles") + v("tries") * 17 + v("tryAssists") * 5 + v("handlingErrors") * -2;
    return std::nearbyint(r + std::min(30, std::max(0, margin) * 2));
}

inline int sum(const Team& t, const char* key) {
    int n = 0;
    for (const Player& p : t.players)
        n += p.s.value(QString::fromLatin1(key));
    return n;
}

// {home, away: {id, teamIndex, name, logoUrl, abbreviation, colour, score}, stadium, date, time,
//  rows: [{label, home, away, share (home's part, 0-1; -1 = no bar)}],
//  homePlayers, awayPlayers: [{shirt, id, name, role, playerIndex, tries, tryAssists, goals, runs, metres, tackles,
//                              missed, lineBreaks, offloads, errors, minutes, rating, best}],
//  motm: {name, team, rating}}
inline QVariantMap build(const Team& home, const Team& away, const QString& stadium, const QString& date,
                         const QString& time, bool rll3) {
    using namespace Qt::StringLiterals;
    QVariantMap out;
    const auto teamMap = [](const Team& t) {
        return QVariantMap{{u"id"_s, t.id}, {u"teamIndex"_s, t.teamIndex}, {u"name"_s, t.name}, {u"location"_s, t.location},
                           {u"club"_s, t.club}, {u"logoUrl"_s, t.logoUrl},
                           {u"abbreviation"_s, t.abbreviation}, {u"colour"_s, t.colour}, {u"score"_s, t.score}};
    };
    out[u"ok"_s] = true;
    out[u"home"_s] = teamMap(home);
    out[u"away"_s] = teamMap(away);
    out[u"stadium"_s] = stadium;
    out[u"date"_s] = date;
    out[u"time"_s] = time;

    // Team v team.
    QVariantList rows;
    const auto row = [&rows](const QString& label, double h, double a, const QString& hs = {}, const QString& as = {}) {
        const double total = h + a;
        rows << QVariantMap{{u"label"_s, label}, {u"home"_s, hs.isEmpty() ? QString::number(h) : hs},
                            {u"away"_s, as.isEmpty() ? QString::number(a) : as},
                            {u"share"_s, total > 0 ? h / total : 0.5}};
    };
    const auto pct = [](int a, int b) { return b > 0 ? int(std::lround(a * 100.0 / b)) : 0; };
    const auto team = [](const Team& t, const char* k) { return t.t.value(QString::fromLatin1(k)); };
    row(u"Score"_s, home.score, away.score);
    row(u"Tries"_s, team(home, "tries"), team(away, "tries"));
    row(u"Goals"_s, sum(home, "conversions"), sum(away, "conversions"),
        u"%1/%2"_s.arg(sum(home, "conversions")).arg(sum(home, "conversionAttempts")),
        u"%1/%2"_s.arg(sum(away, "conversions")).arg(sum(away, "conversionAttempts")));
    row(u"Penalty Goals"_s, sum(home, "penaltyGoals"), sum(away, "penaltyGoals"));
    row(u"Field Goals"_s, team(home, "fieldgoal"), team(away, "fieldgoal"));
    // The C# editor's completion rate: completionAttempts / completions (the sets completed of those played).
    const int hc = team(home, "completionAttempts"), hs = team(home, "completions");
    const int ac = team(away, "completionAttempts"), as = team(away, "completions");
    row(u"Completions"_s, pct(hc, hs), pct(ac, as), u"%1/%2  (%3%)"_s.arg(hc).arg(hs).arg(pct(hc, hs)),
        u"%1/%2  (%3%)"_s.arg(ac).arg(as).arg(pct(ac, as)));
    {   // Drawn as a ring per team in the match view: the rates and the counts on their own.
        QVariantMap r = rows.last().toMap();
        r[u"style"_s] = u"donut"_s;
        r[u"homePct"_s] = pct(hc, hs);
        r[u"awayPct"_s] = pct(ac, as);
        r[u"homeDetail"_s] = u"%1/%2"_s.arg(hc).arg(hs);
        r[u"awayDetail"_s] = u"%1/%2"_s.arg(ac).arg(as);
        rows.last() = r;
    }
    const int hp = team(home, "timeInPossession"), ap = team(away, "timeInPossession");
    row(u"Time In Possession"_s, hp, ap, u"%1%"_s.arg(pct(hp, hp + ap)), u"%1%"_s.arg(pct(ap, hp + ap)));
    {   // A bar split in the teams' colours.
        QVariantMap r = rows.last().toMap();
        r[u"style"_s] = u"split"_s;
        rows.last() = r;
    }
    row(u"Errors"_s, team(home, "errors"), team(away, "errors"));
    row(u"Penalties Conceded"_s, sum(home, "penaltiesConceded"), sum(away, "penaltiesConceded"));
    row(u"Runs"_s, sum(home, "allRuns"), sum(away, "allRuns"));
    row(u"Run Metres"_s, sum(home, "allRunMetres"), sum(away, "allRunMetres"));
    row(u"Line Breaks"_s, team(home, "lineBreaks"), team(away, "lineBreaks"));
    row(u"Tackle Breaks"_s, sum(home, "tackleBreaks"), sum(away, "tackleBreaks"));
    row(u"Offloads"_s, sum(home, "offloads"), sum(away, "offloads"));
    row(u"Kicks"_s, sum(home, "kicksInPlay"), sum(away, "kicksInPlay"));
    row(u"Kick Metres"_s, sum(home, "kickMetresGained"), sum(away, "kickMetresGained"));
    const int ht = team(home, "tackles"), hm = team(home, "missTackles");
    const int at = team(away, "tackles"), am = team(away, "missTackles");
    row(u"Tackles Made"_s, ht, at);
    row(u"Missed Tackles"_s, hm, am);
    row(u"Defence"_s, pct(ht, ht + hm), pct(at, at + am), u"%1%"_s.arg(pct(ht, ht + hm)), u"%1%"_s.arg(pct(at, at + am)));
    out[u"rows"_s] = rows;

    // The players (by shirt), with their match ratings.
    QString motmName;
    QString motmTeam;
    QString motmLogo;
    QString motmColour;
    QString motmAbbreviation;
    int motmShirt = 0;
    double motm = -1e9;
    const auto players = [&](const Team& t, const Team& other) {
        QVariantList list;
        const int margin = t.score - other.score;
        // The shirt for a lineup slot: RLL4 keeps the halfback (7) at 5 and the five-eighth (6) at 6; RLL3 the backs
        // as fullback, left wing, left centre, right centre, right wing.
        const auto shirtOf = [rll3](int position) {
            static const int rll3Backs[] = {1, 5, 4, 3, 2};
            if (rll3 && position >= 0 && position < 5)
                return rll3Backs[position];
            const int shirt = position + 1;
            return !rll3 && shirt == 6 ? 7 : !rll3 && shirt == 7 ? 6 : shirt;
        };
        QList<Player> sorted = t.players;
        std::stable_sort(sorted.begin(), sorted.end(), [&shirtOf](const Player& a, const Player& b) { return shirtOf(a.position) < shirtOf(b.position); });
        for (const Player& p : std::as_const(sorted)) {
            const double r = rating(p, margin, rll3);
            const int shirt = shirtOf(p.position);
            if (r > motm) {
                motm = r;
                motmName = p.name;
                motmTeam = t.name;
                motmLogo = t.logoUrl;
                motmColour = t.colour;
                motmAbbreviation = t.abbreviation;
                motmShirt = shirt;
            }
            const auto v = [&p](const char* k) { return p.s.value(QString::fromLatin1(k)); };
            list << QVariantMap{{u"shirt"_s, shirt}, {u"id"_s, p.id}, {u"name"_s, p.name}, {u"role"_s, p.role},
                                {u"playerIndex"_s, p.playerIndex}, {u"tries"_s, v("tries")}, {u"tryAssists"_s, v("tryAssists")},
                                {u"goals"_s, u"%1/%2"_s.arg(v("conversions") + v("penaltyGoals")).arg(v("conversionAttempts") + v("penaltyGoalAttempts"))},
                                {u"conversions"_s, v("conversions")}, {u"penaltyGoals"_s, v("penaltyGoals")},
                                {u"fieldGoals"_s, v("fieldGoals")}, {u"runs"_s, v("allRuns")}, {u"metres"_s, v("allRunMetres")}, {u"tackles"_s, v("tackles")},
                                {u"missed"_s, v("missedTackles")}, {u"lineBreaks"_s, v("lineBreaks")}, {u"offloads"_s, v("offloads")},
                                {u"errors"_s, v("handlingErrors") + v("knockOns")}, {u"minutes"_s, v("minutesPlayed")},
                                {u"rating"_s, r}};
        }
        return list;
    };
    out[u"homePlayers"_s] = players(home, away);
    out[u"awayPlayers"_s] = players(away, home);
    out[u"motm"_s] = QVariantMap{{u"name"_s, motmName}, {u"team"_s, motmTeam}, {u"rating"_s, motm}, {u"shirt"_s, motmShirt},
                                 {u"logoUrl"_s, motmLogo}, {u"colour"_s, motmColour}, {u"abbreviation"_s, motmAbbreviation}};
    return out;
}

} // namespace CareerMatch

#endif // CAREER_MATCH_STATS_H
