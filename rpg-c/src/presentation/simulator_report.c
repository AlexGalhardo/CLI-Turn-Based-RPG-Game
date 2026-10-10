#include "presentation/simulator_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLUMNS 12

static const char *const HEADER[COLUMNS] = {
    "vocation",
    "difficulty",
    "runs",
    "wins",
    "win %",
    "min",
    "p10",
    "median",
    "p90",
    "max",
    "avg lvl",
    "top killers",
};

typedef struct {
	char *cells[COLUMNS];
} Row;

static char *number_text(int64_t value, const char *suffix) {
	StrBuf text = {0};
	sb_appendf(&text, "%lld%s", (long long)value, suffix);
	return sb_take(&text);
}

static Row summary_row(const SimulationSummary *summary, const GameData *data) {
	StrBuf killers = {0};
	for (int i = 0; i < summary->top_killer_count; i++) {
		sb_appendf(&killers, "%s%s (%lld)", i > 0 ? ", " : "",
		    data_creature(data, summary->top_killers[i].monster_id)->name, (long long)summary->top_killers[i].count);
	}
	Row row = {{
	    xstrdup(summary->vocation),
	    xstrdup(summary->difficulty),
	    number_text(summary->runs, ""),
	    number_text(summary->wins, ""),
	    number_text(summary_win_rate_pct(summary), "%"),
	    number_text(summary->min_round, ""),
	    number_text(summary->p10_round, ""),
	    number_text(summary->median_round, ""),
	    number_text(summary->p90_round, ""),
	    number_text(summary->max_round, ""),
	    number_text(summary->mean_level, ""),
	    sb_take(&killers),
	}};
	return row;
}

static void append_repeated(StrBuf *out, char character, size_t times) {
	for (size_t i = 0; i < times; i++) {
		sb_append_char(out, character);
	}
}

// Cells padded to the column width and joined by two spaces; trailing spaces are dropped.
static void append_row(StrBuf *out, const char *const *cells, const size_t *widths) {
	size_t start = out->length;
	for (int column = 0; column < COLUMNS; column++) {
		if (column > 0) {
			sb_append(out, "  ");
		}
		sb_append(out, cells[column]);
		append_repeated(out, ' ', widths[column] - utf8_length(cells[column]));
	}
	while (out->length > start && out->data[out->length - 1] == ' ') {
		out->data[--out->length] = '\0';
	}
}

void render_report(StrBuf *out, const SimulationSummary *summaries, size_t count, const GameData *data) {
	Row *rows = xcalloc(count, sizeof(Row));
	size_t widths[COLUMNS];
	for (int column = 0; column < COLUMNS; column++) {
		widths[column] = utf8_length(HEADER[column]);
	}
	for (size_t i = 0; i < count; i++) {
		rows[i] = summary_row(&summaries[i], data);
		for (int column = 0; column < COLUMNS; column++) {
			size_t length = utf8_length(rows[i].cells[column]);
			widths[column] = length > widths[column] ? length : widths[column];
		}
	}

	append_row(out, HEADER, widths);
	sb_append_char(out, '\n');
	for (int column = 0; column < COLUMNS; column++) {
		if (column > 0) {
			sb_append(out, "  ");
		}
		append_repeated(out, '-', widths[column]);
	}
	for (size_t i = 0; i < count; i++) {
		sb_append_char(out, '\n');
		append_row(out, (const char *const *)rows[i].cells, widths);
		for (int column = 0; column < COLUMNS; column++) {
			free(rows[i].cells[column]);
		}
	}
	free(rows);
}
