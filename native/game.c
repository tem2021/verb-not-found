#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include "messages.h"

#define ESC_CLEAR       "\033[2J\033[H"
#define ESC_BOLD        "\033[1m"
#define ESC_DIM         "\033[2m"
#define ESC_RESET       "\033[0m"
#define ESC_HIDE_CURSOR "\033[?25l"
#define ESC_SHOW_CURSOR "\033[?25h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define INPUT_SIZE 256
#define SCREEN_WIDTH 72
#define TEXT_WIDTH 68
#define MESSAGE_WIDTH 68
#define MESSAGE_INDENT 4
#define COMMAND_WIDTH 28

enum room {
	ROOM_CLASSROOM,
	ROOM_CORRIDOR,
	ROOM_LIBRARY,
	ROOM_COURT
};

enum learned_word {
	WORD_THANK,
	WORD_MEET,
	WORD_STAY,
	WORD_EXPRESS,
	WORD_REMEMBER,
	WORD_WISH,
	WORD_PLAY,
	WORD_COUNT
};

enum library_choice {
	SHELF_NONE,
	SHELF_PAST,
	SHELF_PRESENT,
	SHELF_FUTURE
};

struct game {
	enum room room;
	enum library_choice shelf;
	bool learned[WORD_COUNT];
	bool shown[sizeof(messages) / sizeof(messages[0])];
	bool delivered[sizeof(messages) / sizeof(messages[0])];
	bool bundle_read[4];
	bool comma_taken;
	bool chalk_available;
	bool chalk_taken;
	bool envelopes_delivered;
	bool met_someone;
	bool expressed;
	bool book_taken;
	bool book_damaged;
	bool racket_ready;
	bool racket_is_chalk;
	bool shuttle_solved;
	bool and_available;
	bool and_taken;
	bool ended;
	bool fast;
	bool ansi;
	unsigned int turns;
	unsigned int hint_level[4];
	char last_command[INPUT_SIZE];
};

static const char *const word_names[WORD_COUNT] = {
	"THANK", "MEET", "STAY", "EXPRESS", "REMEMBER", "WISH", "PLAY"
};

/* Each questionnaire record occurs in exactly one room. */
static const size_t classroom_messages[] = { 10, 7, 3, 15 };
static const size_t corridor_messages[] = { 1, 9, 14, 16, 8 };
static const size_t library_messages[] = { 2, 5, 12, 11 };
static const size_t court_messages[] = { 13, 6, 4, 0, 17 };

/*
 * Display-only corrections.  The questionnaire text in messages.h remains
 * untouched, so every edit can still be checked against the CSV source.
 * These fix spelling, punctuation, and grammar only; they do not rewrite the
 * students' ideas or tone.  Private messages deliberately remain absent.
 */
static const char *const corrected_bodies[] = {
	"To Teacher Wang, I’d like to say: I’m truly grateful to have a "
	"teacher as excellent as you. You are adept at discovering every "
	"student’s bright spots and excel at guiding and inspiring us. I look "
	"forward to having your classes later on. May everything go well with "
	"your work, and may you be happy every single day.\n\n"
	"And to the whole EPC 5 class: It’s a pleasure to get to know all of "
	"you. You’ve made the past two weeks of learning fun and fulfilling. "
	"May each of us reach our goals and fulfill our dreams in the days ahead.",
	NULL,
	"It's a very precious memory for me.\n\nThank you, Bobbie!!!",
	"Bobbie is a wonderful, patient, and skillful teacher! I feel so lucky "
	"to meet her at the beginning of my university journey! And I like all "
	"my classmates! ❤️ All of you are kind and easygoing! I hope all of "
	"you have colorful university lives!",
	"Thanks to all of our classmates and our peer leader. Thanks, Bobbie, "
	"for letting us study while having fun, make new friends, and have a "
	"head start in university life.",
	"Hi, Bobbie; hi, peer leader; hi, my classmates. I am really, really "
	"glad to have met you all and had a wonderful and unforgettable EPC "
	"course. May all of us be happy every day and realize our dreams. See you!",
	"Thanks for your company and for learning together—for not just helping "
	"me integrate into the new environment, but letting me know someone I "
	"can empathize with. Thank you all!",
	"I enjoyed the time with you all, and thank you for making me a better person.",
	"Thank you for your support and guidance. Because of you, I have the "
	"courage to express myself even though I have some difficulties with "
	"pronunciation and listening.\n\nThank you very much!",
	NULL,
	"Thank you for your guidance in spoken English.",
	"Congratulations! And thank you, Miss Bobbie, for your company.",
	"I love you all 🥹🫶🏻—an unforgettable memory in my mind forever. 😸🫰🏻",
	"明天下午打不打羽毛球",
	NULL,
	"Enjoy the days spent with you guys. Let’s stay in touch!",
	NULL,
	"祝大家在新的学习旅程中越来越好！美团老鼠将陪伴你们的欢乐～"
};

static struct game game;

static void ending_details(void);
static void play_object(const char *object);
static void finish_ending(const char *sentence);
static void show_final_blackboard(const char *sentence);

static void
restore_terminal(void)
{
	if (game.ansi)
		fputs(ESC_SHOW_CURSOR ESC_RESET, stdout);
	fflush(stdout);
}

static void
on_signal(int signo)
{
	restore_terminal();
	_exit(128 + signo);
}

static void
sleep_ms(long ms)
{
	struct timespec delay;

	if (game.fast || ms <= 0)
		return;
	delay.tv_sec = ms / 1000;
	delay.tv_nsec = ms % 1000 * 1000000L;
	while (nanosleep(&delay, &delay) == -1 && errno == EINTR)
		;
}

static void
print_rule(void)
{
	unsigned int i;

	for (i = 0; i < SCREEN_WIDTH; ++i)
		putchar('-');
	putchar('\n');
}

static void
clear_screen(void)
{
#ifdef __EMSCRIPTEN__
	puts("@@VNF_CLEAR@@");
#else
	if (game.ansi)
		fputs(ESC_CLEAR, stdout);
	else {
		putchar('\n');
		print_rule();
	}
#endif
}

static void
scene_break(void)
{
	putchar('\n');
	if (game.ansi)
		fputs(ESC_DIM, stdout);
	print_rule();
	if (game.ansi)
		fputs(ESC_RESET, stdout);
}

static void
heading(const char *text)
{
	if (game.ansi)
		fputs(ESC_BOLD, stdout);
	puts(text);
	if (game.ansi)
		fputs(ESC_RESET, stdout);
	putchar('\n');
}

static void
pause_message(void)
{
#ifdef __EMSCRIPTEN__
	puts("@@VNF_PAUSE:continue@@");
	return;
#else
	char line[16];

	if (game.fast)
		return;
	if (game.ansi)
		fputs(ESC_DIM, stdout);
	fputs("\n[Enter] continue", stdout);
	if (game.ansi)
		fputs(ESC_RESET, stdout);
	fflush(stdout);
	if (fgets(line, sizeof(line), stdin) == NULL)
		exit(EXIT_SUCCESS);
#endif
}

static void
pause_scene(const char *prompt)
{
#ifdef __EMSCRIPTEN__
	printf("@@VNF_PAUSE:%s@@\n", prompt);
	return;
#else
	char line[16];

	if (game.fast)
		return;
	if (game.ansi)
		fputs(ESC_DIM, stdout);
	printf("\n[Enter] %s", prompt);
	if (game.ansi)
		fputs(ESC_RESET, stdout);
	fflush(stdout);
	if (fgets(line, sizeof(line), stdin) == NULL)
		exit(EXIT_SUCCESS);
#endif
}

static time_t
show_local_time(void)
{
	char date[16];
	char zone[32];
	time_t now = time(NULL);
	struct tm local;
	unsigned int hour;

	if (now == (time_t)-1 || localtime_r(&now, &local) == NULL) {
		puts("LOCAL SYSTEM TIME: unavailable");
		return (time_t)-1;
	}
	hour = (unsigned int)local.tm_hour % 12;
	if (hour == 0)
		hour = 12;
	if (strftime(date, sizeof(date), "%Y-%m-%d", &local) == 0)
		snprintf(date, sizeof(date), "date unknown");
	if (strftime(zone, sizeof(zone), "%Z", &local) == 0)
		zone[0] = '\0';
	printf("LOCAL SYSTEM TIME\n\n%u:%02d:%02d %s\n%s%s%s\n", hour,
	    local.tm_min, local.tm_sec, local.tm_hour < 12 ? "A.M." : "P.M.",
	    date, zone[0] == '\0' ? "" : "  ", zone);
	return now;
}

static bool
contains(const char *text, const char *part)
{
	return strstr(text, part) != NULL;
}

static void
normalize(char *text)
{
	char *src = text;
	char *dst = text;
	bool space = true;

	while (*src != '\0') {
		unsigned char c = (unsigned char)*src++;

		if (c == '\n' || c == '\r' || c == '\t')
			c = ' ';
		if (c < 0x80)
			c = (unsigned char)tolower(c);
		if (c == ' ') {
			if (!space) {
				*dst++ = ' ';
				space = true;
			}
		} else {
			*dst++ = (char)c;
			space = false;
		}
	}
	if (dst > text && dst[-1] == ' ')
		--dst;
	*dst = '\0';
}

static void
print_indent(unsigned int indent)
{
	while (indent-- > 0)
		putchar(' ');
}

static unsigned int
display_width_n(const char *text, size_t length)
{
	mbstate_t state = { 0 };
	unsigned int width = 0;
	size_t offset = 0;

	while (offset < length) {
		wchar_t wc;
		size_t used = mbrtowc(&wc, text + offset, length - offset, &state);
		int cells;

		if (used == (size_t)-1 || used == (size_t)-2) {
			memset(&state, 0, sizeof(state));
			++offset;
			++width;
			continue;
		}
		if (used == 0)
			break;
		cells = wcwidth(wc);
		width += cells < 0 ? 1U : (unsigned int)cells;
		offset += used;
	}
	return width;
}

static unsigned int
display_width(const char *text)
{
	return display_width_n(text, strlen(text));
}

static void
center_text(const char *text)
{
	unsigned int width = display_width(text);

	if (width < SCREEN_WIDTH)
		print_indent((SCREEN_WIDTH - width) / 2);
	puts(text);
}

static void
print_wrapped_text(const char *text, unsigned int width, unsigned int indent)
{
	unsigned int column = indent;
	bool line_started = false;

	print_indent(indent);
	while (*text != '\0') {
		const char *word;
		size_t length;
		unsigned int word_width;
		unsigned int newlines = 0;

		while (*text == ' ' || *text == '\t' || *text == '\n') {
			if (*text == '\n')
				++newlines;
			++text;
		}
		if (newlines >= 2 && *text != '\0') {
			puts("\n");
			print_indent(indent);
			column = indent;
			line_started = false;
		}
		if (*text == '\0')
			break;
		word = text;
		while (*text != '\0' && *text != ' ' && *text != '\t' &&
		    *text != '\n')
			++text;
		length = (size_t)(text - word);
		word_width = display_width_n(word, length);
		if (line_started && column + 1 + word_width > width) {
			putchar('\n');
			print_indent(indent);
			column = indent;
			line_started = false;
		}
		if (line_started) {
			putchar(' ');
			++column;
		}
		fwrite(word, 1, length, stdout);
		column += word_width;
		line_started = true;
	}
	putchar('\n');
}

static void
prose(const char *text)
{
	print_wrapped_text(text, TEXT_WIDTH, 0);
}

static void
command_row(const char *command, const char *description)
{
	printf("  %-*s%s\n", COMMAND_WIDTH, command, description);
}

static const char *
skip_words(const char *line, unsigned int count)
{
	while (count-- > 0) {
		while (*line == ' ')
			++line;
		while (*line != '\0' && *line != ' ')
			++line;
	}
	while (*line == ' ')
		++line;
	return line;
}

static size_t
delivered_count(void)
{
	size_t count = 0;
	size_t i;

	for (i = 0; i < message_count; ++i) {
		if (game.delivered[i])
			++count;
	}
	return count;
}

static size_t
learned_count(void)
{
	size_t count = 0;
	size_t i;

	for (i = 0; i < WORD_COUNT; ++i) {
		if (game.learned[i])
			++count;
	}
	return count;
}

static void
learn_word(enum learned_word word)
{
	if (game.learned[word])
		return;
	game.learned[word] = true;
	printf("\nVERB RECOVERED: %s\n", word_names[word]);
	sleep_ms(500);
}

static void
show_parser_commands(void)
{
	command_row("LOOK / L", "describe the room");
	command_row("EXAMINE <noun> / X", "inspect something");
	command_row("TAKE <noun> / GET", "carry something");
	command_row("READ <noun>", "read something");
	command_row("GO <direction> / N S E W", "move");
	command_row("GIVE <noun>", "deliver something");
	command_row("USE <noun> AS <role>", "let one noun serve as another");
	command_row("WRITE <words>", "write on something");
}

static void
show_meta_commands(void)
{
	command_row("INVENTORY / I", "list objects and progress");
	command_row("WORDS / VERBS", "list recovered vocabulary");
	command_row("HINT", "receive progressive, local help");
	command_row("HELP / INFO / ?", "show parser instructions");
	command_row("AGAIN / G", "repeat the previous command");
	command_row("WAIT / Z", "allow one turn to pass");
	command_row("MAP", "show known room connections");
	command_row("SCORE", "show progress and turn count");
	command_row("QUIT / Q", "leave the game");
}

static void
show_words(void)
{
	size_t i;
	size_t learned = learned_count();

	puts("ACTIONS    LOOK/L  EXAMINE/X  TAKE  READ  GO/N/S/E/W  GIVE  USE  WRITE");
	fputs("RECOVERED  ", stdout);
	if (learned == 0)
		fputs("none  ", stdout);
	else {
		for (i = 0; i < WORD_COUNT; ++i) {
			if (!game.learned[i])
				continue;
			printf("%s  ", word_names[i]);
		}
	}
	printf("(%zu/%d)\n", learned, WORD_COUNT);
	puts("CONTROL    INVENTORY/I  HELP/?  HINT  AGAIN/G  WAIT/Z  QUIT/Q");
}

static void
show_inventory(void)
{
	bool empty = true;

	puts("You are carrying:");
	if (game.comma_taken) {
		puts("  a comma");
		empty = false;
	}
	if (game.chalk_taken) {
		puts(game.racket_ready && !game.book_taken ?
		    "  half a piece of chalk (recently a racket)" :
		    "  a piece of chalk");
		empty = false;
	}
	if (game.book_taken) {
		const char *name = game.shelf == SHELF_PAST ? "a book of PAST" :
		    game.shelf == SHELF_FUTURE ? "a blank book of FUTURE" :
		    "a book called PRESENT";

		printf("  %s%s\n", name, game.book_damaged ? " (badly returned)" : "");
		empty = false;
	}
	if (game.and_taken) {
		puts("  the conjunction AND");
		empty = false;
	}
	if (empty)
		puts("  nothing");
	printf("\nVocabulary: %zu / %d human verbs recovered.\n",
	    learned_count(), WORD_COUNT);
	printf("Messages:   %zu / %zu delivered.\n", delivered_count(),
	    message_count);
}

static void
describe_classroom(void)
{
	heading("ROOM OF NOUNS");
	prose("Twenty-five CHAIRS are neither in rows nor quite in a circle. "
	    "There is a TEACHER'S DESK, a BLACKBOARD, one paper CUP, and no "
	    "visible activity. The nouns have survived. The verbs have not.");
	putchar('\n');
	prose("A strip of TAPE on the floor marks a place where someone once stood.");
	if (!game.bundle_read[ROOM_CLASSROOM])
		puts("A bundle of MESSAGES rests on the teacher's desk.");
	if (!game.comma_taken)
		puts("A small COMMA lies beneath the blackboard, looking unemployed.");
	if (game.chalk_available && !game.chalk_taken)
		puts("A piece of CHALK has appeared on the desk.");
	if (game.and_taken && game.shuttle_solved) {
		puts("\nOn the blackboard is an unfinished sentence:\n");
		center_text("WE __________.");
		putchar('\n');
		prose("The SOUTH door is open. It will not count as an ending until "
		    "the sentence is complete.");
	} else {
		putchar('\n');
		prose("A doorway leads EAST into a corridor of uncertain pronouns.");
	}
}

static void
describe_corridor(void)
{
	heading("PRONOUN CORRIDOR");
	prose("Every nameplate in this corridor has been replaced by SOMEONE. "
	    "Someone is waiting beside someone else. Neither the parser nor the "
	    "architecture is willing to say who is who. Through the windows, the "
	    "late-morning light makes the empty paths look recently occupied.");
	putchar('\n');
	if (!game.bundle_read[ROOM_CORRIDOR])
		puts("A bundle of public and SEALED MESSAGES sits on a narrow table.");
	else if (!game.envelopes_delivered)
		puts("Four SEALED ENVELOPES still wait for their named recipients.");
	if (!game.met_someone)
		puts("SOMEONE watches you with the patience of an unfinished question.");
	else if (!game.expressed)
		puts("SOMEONE is waiting for you to finish introducing yourself.");
	putchar('\n');
	prose("The Room of Nouns is WEST. The Library of Tenses is NORTH.");
}

static void
describe_library(void)
{
	heading("LIBRARY OF TENSES");
	prose("Three shelves run from floor to ceiling. PAST is full and dusty. "
	    "PRESENT keeps rewriting its own catalogue. FUTURE contains several "
	    "beautifully bound volumes with nothing inside them.");
	putchar('\n');
	if (!game.bundle_read[ROOM_LIBRARY])
		puts("A bundle of MESSAGES has been used as a bookmark.");
	if (game.shelf == SHELF_NONE)
		puts("You may take one book: REMEMBER PAST, TAKE PRESENT, or WISH FUTURE.");
	else
		puts("The other two shelves have politely become unavailable.");
	putchar('\n');
	prose("The Pronoun Corridor is SOUTH. A Conditional Court is EAST.");
}

static void
describe_court(void)
{
	heading("CONDITIONAL COURT");
	prose("A badminton SHUTTLECOCK hangs motionless above a NET. The scoreboard "
	    "reads: IF SOMEONE SERVES, THEN SOMEONE MUST RETURN IT.");
	putchar('\n');
	if (!game.bundle_read[ROOM_COURT])
		puts("The final bundle of MESSAGES is tucked beneath the scoreboard.");
	if (!game.shuttle_solved)
		puts("There is no RACKET. The court appears to consider this your problem.");
	else if (!game.and_taken)
		puts("The word AND lies where the shuttlecock landed.");
	else
		puts("The scoreboard now reads: LEAVE AND STAY. It declines to elaborate.");
	putchar('\n');
	prose("The Library of Tenses is WEST. A shortcut to the classroom is SOUTH.");
}

static void
describe_room(void)
{
	char left[32];
	char right[48];
	int padding;

	scene_break();
	snprintf(left, sizeof(left), "TURN %u", game.turns);
	snprintf(right, sizeof(right), "WORDS %zu/%d   MESSAGES %zu/%zu",
	    learned_count(), WORD_COUNT, delivered_count(), message_count);
	padding = SCREEN_WIDTH - (int)strlen(left) - (int)strlen(right);
	if (game.ansi)
		fputs(ESC_DIM, stdout);
	printf("%s%*s%s\n\n", left, padding > 0 ? padding : 1, "", right);
	if (game.ansi)
		fputs(ESC_RESET, stdout);
	switch (game.room) {
	case ROOM_CLASSROOM:
		describe_classroom();
		break;
	case ROOM_CORRIDOR:
		describe_corridor();
		break;
	case ROOM_LIBRARY:
		describe_library();
		break;
	case ROOM_COURT:
		describe_court();
		break;
	}
}

static void
show_one_message(size_t index)
{
	const struct message *message = &messages[index];

	printf("\nMESSAGE %02u\n", message->source_id);
	if (message->access == MESSAGE_PUBLIC) {
		printf("FROM: %s\nTO:   %s\n\n", message->from, message->to);
		print_wrapped_text(corrected_bodies[index], MESSAGE_WIDTH,
		    MESSAGE_INDENT);
		game.delivered[index] = true;
	} else {
		printf("FROM: hidden\nTO:   %s\n\n", message->to);
		print_wrapped_text("[SEALED PRIVATE MESSAGE]\n\n"
		    "The body is not present in the game.", MESSAGE_WIDTH,
		    MESSAGE_INDENT);
	}
	game.shown[index] = true;
	pause_message();
}

static void
show_message_bundle(const size_t *indices, size_t count, enum room room)
{
	size_t i;

	if (game.bundle_read[room]) {
		puts("You have already read every public message in this bundle.");
		if (room == ROOM_CORRIDOR && !game.envelopes_delivered)
			puts("The four private envelopes remain sealed and undelivered.");
		return;
	}
	for (i = 0; i < count; ++i)
		show_one_message(indices[i]);
	game.bundle_read[room] = true;

	puts("\nThe messages are not polished spells. They work anyway.");
	switch (room) {
	case ROOM_CLASSROOM:
		learn_word(WORD_THANK);
		learn_word(WORD_MEET);
		learn_word(WORD_STAY);
		break;
	case ROOM_CORRIDOR:
		learn_word(WORD_EXPRESS);
		puts("\nFour private messages are still sealed. Their recipients remain private;");
		puts("their contents are not yours to inspect.");
		break;
	case ROOM_LIBRARY:
		learn_word(WORD_REMEMBER);
		learn_word(WORD_WISH);
		break;
	case ROOM_COURT:
		learn_word(WORD_PLAY);
		break;
	}
}

static void
read_messages(void)
{
	switch (game.room) {
	case ROOM_CLASSROOM:
		show_message_bundle(classroom_messages,
		    ARRAY_LEN(classroom_messages), ROOM_CLASSROOM);
		break;
	case ROOM_CORRIDOR:
		show_message_bundle(corridor_messages,
		    ARRAY_LEN(corridor_messages), ROOM_CORRIDOR);
		break;
	case ROOM_LIBRARY:
		show_message_bundle(library_messages,
		    ARRAY_LEN(library_messages), ROOM_LIBRARY);
		break;
	case ROOM_COURT:
		show_message_bundle(court_messages, ARRAY_LEN(court_messages),
		    ROOM_COURT);
		break;
	}
}

static void
deliver_private_messages(void)
{
	size_t i;
	bool any = false;

	if (!game.bundle_read[ROOM_CORRIDOR]) {
		puts("You have not yet discovered any sealed envelopes.");
		return;
	}
	if (game.envelopes_delivered) {
		puts("The private messages have already been delivered without being opened.");
		return;
	}
	for (i = 0; i < ARRAY_LEN(corridor_messages); ++i) {
		size_t index = corridor_messages[i];

		if (messages[index].access != MESSAGE_PRIVATE)
			continue;
		puts("Delivered one sealed message without displaying its recipient.");
		game.delivered[index] = true;
		any = true;
	}
	if (any) {
		game.envelopes_delivered = true;
		puts("\nYou learn nothing about their contents. The delivery is complete.");
		if (game.comma_taken)
			puts("The comma nods. \"Private is not another word for difficult.\"");
	}
}

static bool
word_required(enum learned_word word)
{
	if (game.learned[word])
		return true;
	printf("The parser recognizes the shape of %s, but that verb is missing.\n",
	    word_names[word]);
	puts("Perhaps one of the messages still contains it.");
	return false;
}

static void
take_object(const char *object)
{
	if (contains(object, "comma")) {
		if (game.room != ROOM_CLASSROOM) {
			puts("The comma is not here. It may already be following you.");
			return;
		}
		if (game.comma_taken) {
			puts("You already have the comma. It dislikes possessive grammar.");
			return;
		}
		game.comma_taken = true;
		puts("Taken.\n");
		puts("The comma says, \"Thank you. I am not very good at endings.\"");
		return;
	}
	if (contains(object, "chalk")) {
		if (game.room != ROOM_CLASSROOM || !game.chalk_available) {
			puts("You see no chalk worth taking.");
			return;
		}
		if (game.chalk_taken) {
			puts("You already have the chalk.");
			return;
		}
		game.chalk_taken = true;
		puts("Taken. It is an educational tool, which rarely survives contact");
		puts("with an adventure game.");
		return;
	}
	if (contains(object, "and")) {
		if (game.room != ROOM_COURT || !game.and_available) {
			puts("You cannot take a conjunction that has not yet fallen.");
			return;
		}
		if (game.and_taken) {
			puts("You already have AND. This sentence is becoming crowded.");
			return;
		}
		game.and_taken = true;
		puts("Taken: AND.\n");
		puts("It weighs almost nothing, but can connect incompatible things.");
		return;
	}
	if (contains(object, "past") || contains(object, "present") ||
	    contains(object, "future") || contains(object, "book")) {
		if (game.room != ROOM_LIBRARY) {
			puts("There is no portable book here.");
			return;
		}
		if (!game.bundle_read[ROOM_LIBRARY]) {
			puts("The messages are still marking the relevant page.");
			return;
		}
		if (game.shelf != SHELF_NONE) {
			puts("You have already chosen one tense. The library is strict about this.");
			return;
		}
		if (contains(object, "past")) {
			if (!word_required(WORD_REMEMBER))
				return;
			game.shelf = SHELF_PAST;
			puts("You take a book of PAST. It contains more detail than accuracy.");
		} else if (contains(object, "future")) {
			if (!word_required(WORD_WISH))
				return;
			game.shelf = SHELF_FUTURE;
			puts("You take a book of FUTURE. Every page is blank but numbered.");
		} else {
			game.shelf = SHELF_PRESENT;
			puts("You take PRESENT. The title changes while you are reading it.");
		}
		game.book_taken = true;
		return;
	}
	if (contains(object, "cup")) {
		puts("The cup is empty, but not obviously abandoned. You leave the");
		puts("question of ownership for a less reckless parser.");
		return;
	}
	if (contains(object, "tape")) {
		puts("The tape has already failed to hold one moment in place.");
		puts("Removing it now would seem unnecessarily triumphant.");
		return;
	}
	if (contains(object, "teacher")) {
		puts("The teacher is not currently implemented as portable.");
		return;
	}
	if (contains(object, "door")) {
		puts("The door has strong feelings about remaining part of the building.");
		return;
	}
	puts("You cannot see anything like that which is sensibly portable.");
}

static void
examine_object(const char *object)
{
	if (*object == '\0') {
		puts("What do you want to examine?");
		return;
	}
	if (contains(object, "comma")) {
		puts("A small comma, slightly bent. It has spent its entire career");
		puts("preventing sentences from ending too soon.");
	} else if (contains(object, "blackboard") || contains(object, "board")) {
		puts(game.and_taken ? "It reads: WE __________." :
		    "It reads: WE __________. The missing word has left no forwarding address.");
	} else if (contains(object, "message") || contains(object, "envelope")) {
		if (game.room == ROOM_CORRIDOR && game.bundle_read[ROOM_CORRIDOR] &&
		    !game.envelopes_delivered)
			puts("Four private envelopes. The TO fields are readable; the bodies are sealed.");
		else
			puts("They were written in the previous class. READ MESSAGES to inspect them.");
	} else if (contains(object, "chalk")) {
		puts("White chalk. Long, light, and only very approximately racket-shaped.");
	} else if (contains(object, "someone") || contains(object, "student")) {
		puts("Someone around your age, currently protected by a pronoun and");
		puts("the reasonable expectation that you will introduce yourself first.");
	} else if (contains(object, "past")) {
		puts("The PAST shelf is full. Several memories contradict one another");
		puts("without appearing especially worried about it.");
	} else if (contains(object, "present")) {
		puts("PRESENT rewrites itself each time you look away.");
	} else if (contains(object, "future")) {
		puts("The FUTURE books are blank. This is not the same as being empty.");
	} else if (contains(object, "shuttle") || contains(object, "birdie")) {
		puts("A shuttlecock held above the net by a sentence whose condition");
		puts("has never been fulfilled.");
	} else if (contains(object, "net")) {
		puts("A badminton net. The word AND appears to be caught above it,");
		puts("just out of reach.");
	} else if (contains(object, "door")) {
		puts("A perfectly ordinary door. It is offended that this makes it suspicious.");
	} else if (contains(object, "chair")) {
		puts("Twenty-five chairs, left in an arrangement that must once have made");
		puts("sense to twenty-five people. Empty, they refuse to explain it.");
	} else if (contains(object, "tape") || contains(object, "dubbing")) {
		puts("A strip of masking tape, no longer attached to an explanation.");
	} else if (contains(object, "cup")) {
		puts("A paper cup with one thumbprint near the rim. The parser declines");
		puts("to identify its owner from this evidence.");
	} else if (contains(object, "photo") || contains(object, "photograph")) {
		puts("You see no photograph. For a moment, this feels less like absence");
		puts("than a grammatical tense.");
	} else if (contains(object, "desk")) {
		puts("The teacher's desk. It contains no grades, answers, or portable teachers.");
	} else if (contains(object, "and")) {
		puts("A conjunction capable of making two true things coexist.");
	} else {
		puts("You find no additional detail, though the parser admires your diligence.");
	}
}

static void
move_player(const char *direction)
{
	enum room destination = game.room;
	bool valid = true;

	if (strcmp(direction, "e") == 0)
		direction = "east";
	else if (strcmp(direction, "w") == 0)
		direction = "west";
	else if (strcmp(direction, "n") == 0)
		direction = "north";
	else if (strcmp(direction, "s") == 0)
		direction = "south";

	switch (game.room) {
	case ROOM_CLASSROOM:
		if (strcmp(direction, "east") == 0)
			destination = ROOM_CORRIDOR;
		else
			valid = false;
		if (!game.bundle_read[ROOM_CLASSROOM]) {
			puts("You reach the doorway, but the unwritten messages pull the room");
			puts("back into the present. READ MESSAGES first.");
			return;
		}
		if (!game.comma_taken) {
			puts("The comma lies under the blackboard, trying not to look abandoned.");
			puts("You decide not to leave punctuation behind.");
			return;
		}
		break;
	case ROOM_CORRIDOR:
		if (strcmp(direction, "west") == 0)
			destination = ROOM_CLASSROOM;
		else if (strcmp(direction, "north") == 0) {
			destination = ROOM_LIBRARY;
			if (!game.envelopes_delivered || !game.expressed) {
				puts("The northern pronoun refuses to become a definite direction.");
				puts("There are still messages or introductions unfinished here.");
				return;
			}
		} else
			valid = false;
		break;
	case ROOM_LIBRARY:
		if (strcmp(direction, "south") == 0)
			destination = ROOM_CORRIDOR;
		else if (strcmp(direction, "east") == 0) {
			destination = ROOM_COURT;
			if (game.shelf == SHELF_NONE) {
				puts("The eastern catalogue asks which tense you are carrying.");
				puts("Choose PAST, PRESENT, or FUTURE before leaving.");
				return;
			}
		} else
			valid = false;
		break;
	case ROOM_COURT:
		if (strcmp(direction, "west") == 0)
			destination = ROOM_LIBRARY;
		else if (strcmp(direction, "south") == 0) {
			destination = ROOM_CLASSROOM;
			if (!game.and_taken) {
				puts("The southern shortcut is written as LEAVE AND STAY.");
				puts("You are currently missing part of that sentence.");
				return;
			}
		} else
			valid = false;
		break;
	}
	if (!valid) {
		if (contains(direction, "door"))
			puts("Which door? The parser accepts directions; architecture accepts blame.");
		else if (contains(direction, "home"))
			puts("You think of home. For the moment, it is not a compass direction.");
		else if (contains(direction, "future"))
			puts("The FUTURE is not a direction, despite decades of promotional material.");
		else if (contains(direction, "class"))
			puts("You are already inside the last class in at least one sense.");
		else
			puts("You cannot go that way. Geography retains editorial control.");
		return;
	}
	game.room = destination;
	describe_room();
}

static void
thank_object(const char *object)
{
	if (!word_required(WORD_THANK))
		return;
	if (contains(object, "teacher") || contains(object, "desk")) {
		if (game.room != ROOM_CLASSROOM) {
			puts("Your thanks travel toward the classroom. No receipt is issued.");
			return;
		}
		if (!game.chalk_available) {
			puts("The teacher's chair remains empty.\n");
			puts("A drawer in the desk opens by itself. Inside is a piece of CHALK.");
			game.chalk_available = true;
		} else
			puts("The desk accepts the additional thanks without generating more chalk.");
	} else if (contains(object, "door")) {
		puts("The door appreciates this. It remains a door.");
	} else if (contains(object, "comma")) {
		puts("\"Please,\" says the comma. \"You already picked me up.\"");
	} else if (contains(object, "message")) {
		puts("The messages do not answer, but several of them contain the same verb.");
	} else if (contains(object, "someone") || contains(object, "student") ||
	    contains(object, "classmate")) {
		puts("Someone hears you. This is less visible than a puzzle reward, but real.");
	} else if (contains(object, "chair")) {
		puts("The chair accepts your gratitude on behalf of several exhausted backs.");
	} else if (contains(object, "book")) {
		puts("The book adds your thanks to its acknowledgements without permission.");
	} else if (contains(object, "shuttle") || contains(object, "net")) {
		puts("Sports equipment traditionally prefers applause, but accepts thanks.");
	} else if (*object == '\0') {
		puts("Whom—or what—do you want to THANK?");
	} else
		puts("Your thanks are sincere but insufficiently addressed.");
}

static void
meet_object(const char *object)
{
	if (!word_required(WORD_MEET))
		return;
	if (game.room == ROOM_CORRIDOR &&
	    (contains(object, "someone") || contains(object, "student") ||
	    contains(object, "person"))) {
		if (!game.met_someone) {
			game.met_someone = true;
			puts("You step forward. SOMEONE does the same.\n");
			puts("\"Hello,\" they say. \"Who are you?\"");
			if (game.comma_taken)
				puts("\nThe comma whispers, \"This sounds like an EXPRESS problem.\"");
		} else
			puts("You have met. What remains is the difficult part after hello.");
	} else if (contains(object, "blackboard")) {
		puts("You meet the blackboard. It does not remember your name either.");
	} else if (contains(object, "door")) {
		puts("You and the door have now been formally introduced.");
	} else if (contains(object, "comma")) {
		puts("You have already met the comma. It remembers being taken.");
	} else if (contains(object, "teacher")) {
		puts("You have met the teacher before. Today is part of the evidence.");
	} else if (contains(object, "yourself")) {
		puts("A four-year appointment has been scheduled. Results may vary.");
	} else if (*object == '\0') {
		puts("Whom do you want to MEET?");
	} else
		puts("A meeting requires a more definite participant.");
}

static void
express_object(const char *object)
{
	if (!word_required(WORD_EXPRESS))
		return;
	if (game.room == ROOM_CORRIDOR &&
	    (contains(object, "yourself") || contains(object, "someone") ||
	    contains(object, "person") || *object == '\0')) {
		if (!game.met_someone) {
			puts("You begin expressing yourself to the corridor. The corridor is");
			puts("supportive but recommends meeting a person first.");
			return;
		}
		if (!game.expressed) {
			game.expressed = true;
			puts("You say your name. You mispronounce one syllable, although it is");
			puts("your own name and therefore legally your decision.\n");
			puts("SOMEONE waits until you finish, then tells you theirs.");
			puts("The sign to the NORTH becomes definite: LIBRARY OF TENSES.");
		} else
			puts("You have already introduced yourself. Additional versions are optional.");
	} else if (contains(object, "blackboard")) {
		puts("The blackboard listens without interrupting. Nothing else happens,");
		puts("which may already be more than expected.");
	} else if (contains(object, "door")) {
		puts("The door respects emotional honesty but still prefers hinges.");
	} else if (contains(object, "comma")) {
		puts("The comma listens between your clauses and interrupts neither one.");
	} else if (contains(object, "message")) {
		puts("The messages have already expressed themselves, including the mistakes.");
	} else if (contains(object, "chair")) {
		puts("You express yourself to a chair. It offers excellent attendance.");
	} else
		puts("You express something sincere in an unspecified direction.");
}

static void
choose_shelf(enum library_choice choice)
{
	if (game.room != ROOM_LIBRARY) {
		puts("Tense is currently a grammatical concept rather than a shelf.");
		return;
	}
	if (!game.bundle_read[ROOM_LIBRARY]) {
		puts("The messages are still marking the relevant page. READ them first.");
		return;
	}
	if (game.shelf != SHELF_NONE) {
		puts("You have already chosen one tense. The others recede into grammar.");
		return;
	}
	game.shelf = choice;
	game.book_taken = true;
	if (choice == SHELF_PAST)
		puts("You remember the PAST. A book slides free. Its details disagree,");
	else if (choice == SHELF_FUTURE) {
		puts("You wish toward the FUTURE. A blank book accepts your name without");
		puts("claiming to know the rest.");
	}
	else
		puts("You take PRESENT. The title changes while you are holding it.");
	if (choice == SHELF_PAST)
		puts("but the feeling of having been there appears consistent.");
}

static void
use_object(const char *line)
{
	if ((contains(line, "chalk") || contains(line, "book")) &&
	    (contains(line, "as racket") || contains(line, "as a racket") ||
	    contains(line, "like racket") || contains(line, "like a racket"))) {
		if (game.room != ROOM_COURT) {
			puts("There is no badminton emergency here.");
			return;
		}
		if (contains(line, "chalk")) {
			if (!game.chalk_taken) {
				puts("You are not carrying the chalk.");
				return;
			}
			game.racket_ready = true;
			game.racket_is_chalk = true;
			puts("You hold the chalk like a racket.\n");
			puts("The comma says, \"That is not what chalk is for.\"");
			puts("The court says nothing, which is not the same as disagreement.");
		} else {
			if (!game.book_taken) {
				puts("You are not carrying a book.");
				return;
			}
			game.racket_ready = true;
			game.racket_is_chalk = false;
			puts("You hold the book by its spine. The library objects from the WEST.");
		}
		puts("\nThe shuttlecock is waiting. You can now PLAY BADMINTON.");
		return;
	}
	if (game.room == ROOM_COURT &&
	    (contains(line, "chalk") || contains(line, "book"))) {
		if (contains(line, "with") || contains(line, "hit") ||
		    contains(line, "strike") || contains(line, "swing")) {
			puts("The physical idea is clear, but the court wants a grammatical role.");
			puts("WITH says what you use. AS says what it is allowed to become.");
		} else
			printf("Use the %s AS what?\n",
			    contains(line, "chalk") ? "CHALK" : "BOOK");
		puts("The scoreboard offers the form: USE <OBJECT> AS <ROLE>.");
		return;
	}
	if (contains(line, "comma") && contains(line, "door")) {
		puts("The comma slips under the door, then returns.\n");
		puts("\"There is another room,\" it reports. \"This is rarely surprising.\"");
		return;
	}
	if (contains(line, "and"))
		puts("You use AND to connect this attempt with its failure.");
	else if (contains(line, "comma"))
		puts("The comma asks to be treated as a companion, not stationery.");
	else if (contains(line, "chair"))
		puts("You sit experimentally. The chair performs exactly as advertised.");
	else
		puts("The proposed use is imaginative but not currently supported by physics.");
}

static void
play_object(const char *object)
{
	if (!word_required(WORD_PLAY))
		return;
	if (game.room != ROOM_COURT ||
	    (!contains(object, "badminton") && !contains(object, "shuttle") &&
	    !contains(object, "game") && *object != '\0')) {
		if (contains(object, "comma"))
			puts("The comma moves several centimetres away. \"We just met.\"");
		else if (contains(object, "message"))
			puts("The messages are sincere. Treating them as playing cards seems risky.");
		else if (contains(object, "book"))
			puts("You flip through the book. This is reading with unnecessary wristwork.");
		else if (contains(object, "future"))
			puts("You may play with the future, but the future also gets a turn.");
		else
			puts("This is not currently recognized as a playable game.");
		return;
	}
	if (game.shuttle_solved) {
		puts("The rally has already ended at one-all, which everyone has agreed");
		puts("to describe as a complete tournament.");
		return;
	}
	if (!game.racket_ready) {
		puts("You attempt to serve with your hand.\n");
		puts("The shuttlecock remains suspended. The court displays: RACKET NOT FOUND.");
		puts("A smaller line flickers beneath it: USE <OBJECT> AS RACKET.");
		if (game.chalk_taken)
			puts("The comma looks at the CHALK and then carefully looks away.");
		else if (game.book_taken)
			puts("The book in your inventory has roughly the correct surface area.");
		return;
	}
	game.shuttle_solved = true;
	game.and_available = true;
	if (game.racket_is_chalk) {
		game.chalk_taken = false;
		game.chalk_available = false;
		puts("You serve with the chalk. It breaks cleanly in half, but the");
		puts("shuttlecock crosses the net. Someone unseen returns it.");
	} else {
		game.book_damaged = true;
		puts("You serve with the book. The shuttlecock crosses the net. The");
		puts("book also crosses the net, less intentionally.");
	}
	puts("\nThe shuttlecock strikes the scoreboard. THEN falls off and shatters.");
	puts("A smaller word drops beside your shoe: AND.");
}

static void
remember_object(const char *object)
{
	if (!word_required(WORD_REMEMBER))
		return;
	if (contains(object, "past") || contains(object, "book") || *object == '\0')
		choose_shelf(SHELF_PAST);
	else if (contains(object, "future"))
		puts("You cannot remember the future. The parser appreciates the ambition.");
	else if (contains(object, "someone"))
		puts("You remember that SOMEONE told you a name. The game keeps it private.");
	else if (contains(object, "message"))
		puts("You remember the messages unevenly: one phrase, one typo, one feeling.");
	else if (contains(object, "chair"))
		puts("You remember sitting in it. The chair claims this is mutual.");
	else
		puts("You remember it imperfectly, which appears to be the standard format.");
}

static void
wish_object(const char *object)
{
	if (!word_required(WORD_WISH))
		return;
	if (contains(object, "future") || contains(object, "book") || *object == '\0')
		choose_shelf(SHELF_FUTURE);
	else if (contains(object, "door"))
		puts("You wish the door open. It recommends the emergency verb GO.");
	else if (contains(object, "past"))
		puts("You wish the past were editable. The library has disabled revisions.");
	else if (contains(object, "message")) {
		puts("The message already contains a wish. Adding another makes no guarantee,");
		puts("but does make it less alone.");
	} else
		puts("The wish is recorded without being mistaken for a prediction.");
}

static void
write_ending(const char *line)
{
	if (game.room != ROOM_CLASSROOM || !game.and_taken ||
	    delivered_count() != message_count) {
		puts("There is not yet a complete sentence available to write.");
		return;
	}
	if (contains(line, "go and stay") || contains(line, "continue")) {
		clear_screen();
		heading("WE CONTINUE.");
		puts("The parser considers GO AND STAY.\n");
		puts("CONTRADICTION DETECTED.\n");
		puts("The comma clears its throat.\n");
		puts("\"Only if you think leaving and losing are the same thing.\"\n");
		puts("A cursor appears immediately after the full stop.");
		puts("The classroom door opens. Everyone is permitted to go somewhere else.");
		finish_ending("WE CONTINUE.");
	} else if (contains(line, "we met") || strcmp(line, "met") == 0) {
		clear_screen();
		heading("WE MET.");
		puts("It is past tense, complete, and true.\n");
		puts("The door opens. The comma studies the period for a long moment.");
		puts("\n\"A short sentence,\" it says. \"Not a small one.\"");
		finish_ending("WE MET.");
	} else if (contains(line, "we remember") || strcmp(line, "remember") == 0) {
		clear_screen();
		heading("WE REMEMBER.");
		puts("The sentence makes a promise the parser cannot verify.\n");
		puts("It accepts the sentence anyway. The door opens.");
		puts("Behind you, one name has already been misspelled. It still belongs.");
		finish_ending("WE REMEMBER.");
	} else if (contains(line, "we play") || strcmp(line, "play") == 0) {
		puts("The shuttlecock rolls through the doorway and stops beneath WE.");
		puts("The sentence is plausible, but apparently wants another turn.");
	} else if (contains(line, "we thank") || strcmp(line, "thank") == 0) {
		clear_screen();
		heading("WE THANK.");
		puts("The grammar is unusual. The meaning is not.\n");
		puts("Every empty chair receives the sentence. The classroom door opens.");
		finish_ending("WE THANK.");
	} else if (contains(line, "we wish") || strcmp(line, "wish") == 0) {
		clear_screen();
		heading("WE WISH.");
		puts("The sentence opens onto a future it does not attempt to describe.");
		puts("The door follows its example.");
		finish_ending("WE WISH.");
	} else {
		puts("The chalk pauses above the blank. The sentence knows every word you");
		puts("have recovered, but not what you intend to do with this one.");
	}
}

static void
show_final_blackboard(const char *sentence)
{
	char line[80];
	int content_width = 50;
	int text_width = (int)strlen(sentence) + 1;
	int left = (content_width - text_width) / 2;
	int right = content_width - text_width - left;

	center_text("+--------------------------------------------------+");
	center_text("| BLACKBOARD                               [--:--] |");
	center_text("|                                                  |");
	snprintf(line, sizeof(line), "|%*s%s_%*s|", left, "", sentence,
	    right, "");
	center_text(line);
	center_text("|                                                  |");
	center_text("+--------------------------------------------------+");
}

static void
finish_ending(const char *sentence)
{
	time_t first_seen;
	time_t second_seen;

	pause_scene("let the sentence settle");
	clear_screen();
	heading("AFTER THE LAST CLASS");
	prose("For the first time since you arrived, nothing requires a command.");
	putchar('\n');
	prose("Fourteen messages have been heard aloud. Four more have reached "
	    "their recipients without becoming public. They do not arrange "
	    "themselves into a moral.");
	putchar('\n');
	prose("They remain uneven, specific, and meant by someone.");
	ending_details();

	pause_scene("look at the classroom one last time");
	clear_screen();
	heading("ROOM OF NOUNS");
	show_final_blackboard(sentence);
	putchar('\n');
	prose("Late-morning light has crossed two rows of chairs. The paper cup "
	    "and the strip of tape remain exactly where no one remembers leaving "
	    "them. After the final punctuation, a cursor continues to blink.");
	putchar('\n');
	puts("MESSAGES  18/18");
	puts("VERBS      7/7");
	puts("TIME       not found");

	pause_scene("ask the terminal what time it is");
	clear_screen();
	first_seen = show_local_time();
	putchar('\n');
	prose("The terminal has answered with a time it did not invent. For one "
	    "line, the parser and the classroom occupy the same present.");

	pause_scene("return to the classroom outside the terminal");
	if (!game.fast && first_seen != (time_t)-1 && time(NULL) <= first_seen)
		sleep_ms(1100);
	clear_screen();
	second_seen = show_local_time();
	putchar('\n');
	if (first_seen != (time_t)-1 && second_seen > first_seen)
		prose("The seconds have moved. The story has failed to keep them.");
	else
		prose("The rehearsal clock waits for an audience before moving on.");
	prose("Somewhere ahead of this moment, a class photograph is waiting to "
	    "be taken. The parser cannot EXAMINE it yet.");
	putchar('\n');
	prose("Behind you, the cursor continues blinking for whoever looks back. "
	    "Ahead, the rest of the morning requires no typed command.");
	putchar('\n');
	center_text("[ END ]");
	game.ended = true;
}

static void
ending_details(void)
{
	if (game.shelf == SHELF_PAST)
		puts("\nThe book of PAST remembers the room slightly differently.");
	else if (game.shelf == SHELF_PRESENT)
		puts("\nPRESENT changes its title to NOW, then closes itself.");
	else if (game.shelf == SHELF_FUTURE)
		puts("\nThe blank FUTURE book adds a second page.");
	if (game.book_damaged)
		puts("The library files a complaint about your athletic use of literature.");
}

static void
show_hint(void)
{
	unsigned int level = game.hint_level[game.room]++;

	if (game.room == ROOM_CLASSROOM && !game.and_taken) {
		if (!game.bundle_read[ROOM_CLASSROOM])
			puts(level == 0 ? "The comma looks at the MESSAGES." :
			    "Try READ MESSAGES.");
		else if (!game.comma_taken)
			puts(level == 0 ? "The comma is trying very hard to be TAKE-able." :
			    "Try TAKE COMMA.");
		else if (!game.chalk_taken)
			puts(level == 0 ? "The TEACHER'S DESK may respond to a recovered verb." :
			    "Try THANK TEACHER, then TAKE CHALK. The chalk is optional but useful.");
		else
			puts("GO EAST when you are ready.");
	} else if (game.room == ROOM_CORRIDOR) {
		if (!game.bundle_read[ROOM_CORRIDOR])
			puts(level == 0 ? "The table contains MESSAGES." : "Try READ MESSAGES.");
		else if (!game.envelopes_delivered)
			puts(level == 0 ? "Private messages need delivery, not inspection." :
			    "Try GIVE ENVELOPES TO RECIPIENTS.");
		else if (!game.met_someone)
			puts(level == 0 ? "SOMEONE cannot remain a stranger after MEET." :
			    "Try MEET SOMEONE.");
		else if (!game.expressed)
			puts(level == 0 ? "SOMEONE asked who you are. A recovered verb can answer." :
			    "Try EXPRESS YOURSELF.");
		else
			puts("GO NORTH to the Library of Tenses.");
	} else if (game.room == ROOM_LIBRARY) {
		if (!game.bundle_read[ROOM_LIBRARY])
			puts(level == 0 ? "The MESSAGES are being used as a bookmark." :
			    "Try READ MESSAGES.");
		else if (game.shelf == SHELF_NONE)
			puts(level == 0 ? "Choose one tense. All three choices are valid." :
			    "Try REMEMBER PAST, TAKE PRESENT, or WISH FUTURE.");
		else
			puts("GO EAST to the Conditional Court.");
	} else if (game.room == ROOM_COURT) {
		if (!game.bundle_read[ROOM_COURT])
			puts(level == 0 ? "The final MESSAGES are under the scoreboard." :
			    "Try READ MESSAGES.");
		else if (!game.racket_ready)
			puts(level == 0 ? "An object need not be a racket. It may only need to serve AS one." :
			    game.chalk_taken ? "Try USE CHALK AS RACKET." :
			    "Try USE BOOK AS RACKET.");
		else if (!game.shuttle_solved)
			puts(level == 0 ? "The recovered verb PLAY is unusually relevant." :
			    "Try PLAY BADMINTON.");
		else if (!game.and_taken)
			puts(level == 0 ? "A conjunction has fallen beside your shoe." :
			    "Try TAKE AND.");
		else
			puts("GO SOUTH to return to the classroom.");
	} else {
		if (level == 0)
			puts("The sentence may use one recovered verb, or combine two with AND.");
		else if (level == 1)
			puts("GO and STAY appear to disagree. The comma specializes in that situation.");
		else
			puts("Try GO AND STAY, or WRITE WE REMEMBER for a different ending.");
	}
}

static void
show_help(void)
{
	puts("HOW TO PLAY\n");
	prose("Speak to the story in short English commands. Capitalization does "
	    "not matter, and articles inside noun phrases are usually optional. "
	    "Most commands take one of these forms: VERB, VERB NOUN, or VERB "
	    "NOUN AS NOUN.");
	puts("\nBASIC COMMANDS");
	show_parser_commands();
	puts("\nCONVERSATION");
	command_row("ASK <person> ABOUT <topic>", "ask a specific question");
	command_row("TALK TO <person>", "begin a conversation");
	command_row("SAY HELLO TO <person>", "offer a greeting");
	pause_scene("more");
	puts("\nGAME COMMANDS");
	show_meta_commands();
	putchar('\n');
	prose("Words in CAPITALS are usually worth examining or using. HELP "
	    "explains grammar, not solutions; HINT responds to the current room "
	    "and becomes more specific when repeated.");
	putchar('\n');
	prose("The parser knows more words than this page admits. Optional actions "
	    "such as SLEEP are not required to win, but they may still receive an "
	    "answer. Experimentation is safe.");
}

static void
show_map(void)
{
	puts("                         [LIBRARY OF TENSES]");
	puts("                                  |");
	puts("                                  +--- [CONDITIONAL COURT]");
	puts("                                  |             |");
	puts("[ROOM OF NOUNS] --- [PRONOUN CORRIDOR]         | <SHORTCUT>");
	puts("        +---------------------------------------+");
}

static void
ask_object(const char *line)
{
	if (contains(line, "comma")) {
		if (contains(line, "future"))
			puts("\"I connect what comes before with what comes after,\" says the comma.\n"
			    "\"I do not guarantee either one.\"");
		else if (contains(line, "ending") || contains(line, "period"))
			puts("\"I am punctuation,\" says the comma, \"not closure.\"");
		else if (contains(line, "message"))
			puts("\"Read them,\" says the comma. \"They were not written for decoration.\"");
		else {
			puts("The comma considers the current room.");
			show_hint();
		}
		return;
	}
	if (contains(line, "someone") || contains(line, "student") ||
	    contains(line, "person")) {
		if (!game.met_someone)
			meet_object("someone");
		else if (!game.expressed)
			puts("SOMEONE says, \"You were telling me who you are.\"");
		else
			puts("You talk briefly. Not every conversation needs to become a puzzle.");
		return;
	}
	if (contains(line, "teacher")) {
		puts("The teacher is no longer in the room. The question remains valid.");
		return;
	}
	puts("No one here appears responsible for answering that.");
}

static void
special_failure(const char *line)
{
	if (strcmp(line, "xyzzy") == 0 || strcmp(line, "plugh") == 0)
		puts("A hollow voice says, \"Wrong campus.\"");
	else if (contains(line, "eat comma"))
		puts("The comma moves several centimetres away. \"We just met.\"");
	else if (contains(line, "kiss"))
		puts("Consent has not been established. The action is cancelled.");
	else if (contains(line, "kill") || contains(line, "attack"))
		puts("Violence is not among the verbs worth restoring.");
	else if (strcmp(line, "save") == 0)
		puts("The comma promises to remember where you were. This is not legally a save file.");
	else if (strcmp(line, "undo") == 0)
		puts("The comma can connect clauses, not reverse them.");
	else if (strcmp(line, "time") == 0)
		puts("The clock has no hands. The game is not ready to admit the time.");
	else if (strcmp(line, "version") == 0)
		puts("VERB NOT FOUND, Release 2 / Serial number 260828");
	else if (strcmp(line, "brief") == 0)
		puts("Brief.");
	else if (strcmp(line, "verbose") == 0)
		puts("The room clears its throat. It was already describing everything.");
	else if (contains(line, "open") && contains(line, "private"))
		puts("Private is not another word for difficult. The envelope remains sealed.");
	else if (strcmp(line, "jump") == 0)
		puts("You jump. For half a second, even gravity loses its verb.");
	else if (strcmp(line, "sing") == 0 || contains(line, "sing song"))
		puts("You supply a melody. The empty chairs decline to supply harmony.");
	else if (strcmp(line, "sleep") == 0)
		puts("The room looks as though everyone else tried that by leaving first.");
	else if (strcmp(line, "wake") == 0 || strcmp(line, "wake up") == 0)
		puts("You appear to be awake. The same cannot yet be said of the verbs.");
	else if (strcmp(line, "smell") == 0 || strncmp(line, "smell ", 6) == 0)
		puts("Chalk dust, paper, air conditioning, and the faint idea of lunch.");
	else if (strncmp(line, "touch ", 6) == 0) {
		if (contains(line, "comma"))
			puts("The comma is cool metal—or possibly only cool punctuation.");
		else
			puts("Touched. The noun remains stubbornly material.");
	} else if (strcmp(line, "sit") == 0 || contains(line, "sit on"))
		puts("You sit. One chair becomes occupied; the room does not become less empty.");
	else if (strcmp(line, "dance") == 0)
		puts("You supply the movement. The room supplies no music and no objection.");
	else if (strcmp(line, "study") == 0 || contains(line, "study "))
		puts("An admirable instinct. The parser has misplaced the syllabus.");
	else if (strcmp(line, "laugh") == 0)
		puts("You laugh. Somewhere outside the parser, someone else almost does too.");
	else if (strcmp(line, "cry") == 0)
		puts("The game has no score for this. It waits without looking away.");
	else if (contains(line, "love"))
		puts("That verb is far too irregular for this parser. It remains available.");
	else if (strcmp(line, "listen") == 0)
		puts("Air conditioning, a distant chair, and a clock the game has not described.");
	else if (strcmp(line, "who am i") == 0)
		puts("You are whoever is currently standing at the prompt.");
	else if (contains(line, "object of the game") ||
	    contains(line, "goal of the game"))
		puts("Recover the verbs. Deliver the messages. Leave one sentence behind.");
	else if (strcmp(line, "chair") == 0 || strcmp(line, "comma") == 0 ||
	    strcmp(line, "messages") == 0 || strcmp(line, "door") == 0)
		puts("That is a noun. The crisis has not promoted it. Try VERB NOUN.");
	else
		puts("The parser does not recognize that verb yet. Try WORDS or HELP.");
}

static bool
process_command(const char *input)
{
	char line[INPUT_SIZE];
	const char *object;
	bool turn = true;

	snprintf(line, sizeof(line), "%s", input);
	normalize(line);
	if (*line == '\0')
		return true;
	if (strcmp(line, "again") == 0 || strcmp(line, "g") == 0) {
		char repeated[INPUT_SIZE];

		if (game.last_command[0] == '\0') {
			puts("There is no previous command to repeat.");
			return true;
		}
		snprintf(repeated, sizeof(repeated), "%s", game.last_command);
		printf("(%s)\n", repeated);
		return process_command(repeated);
	}
	snprintf(game.last_command, sizeof(game.last_command), "%s", line);

	if (strcmp(line, "quit") == 0 || strcmp(line, "q") == 0)
		return false;
	if (strcmp(line, "look") == 0 || strcmp(line, "look around") == 0 ||
	    strcmp(line, "l") == 0) {
		describe_room();
		turn = false;
	} else if (strcmp(line, "inventory") == 0 || strcmp(line, "i") == 0) {
		show_inventory();
		turn = false;
	} else if (strcmp(line, "map") == 0) {
		show_map();
		turn = false;
	} else if (strcmp(line, "words") == 0 || strcmp(line, "verbs") == 0 ||
	    strcmp(line, "vocab") == 0) {
		show_words();
		turn = false;
	} else if (strcmp(line, "help") == 0 || strcmp(line, "info") == 0 ||
	    strcmp(line, "commands") == 0 || strcmp(line, "?") == 0) {
		show_help();
		turn = false;
	} else if (strcmp(line, "hint") == 0) {
		show_hint();
		turn = false;
	} else if (strcmp(line, "score") == 0) {
		printf("Vocabulary %zu/%d. Messages %zu/%zu. Turns %u.\n",
		    learned_count(), WORD_COUNT, delivered_count(), message_count,
		    game.turns);
		turn = false;
	} else if (strcmp(line, "north") == 0 || strcmp(line, "n") == 0 ||
	    strcmp(line, "south") == 0 || strcmp(line, "s") == 0 ||
	    strcmp(line, "east") == 0 || strcmp(line, "e") == 0 ||
	    strcmp(line, "west") == 0 || strcmp(line, "w") == 0) {
		move_player(line);
	} else if (strncmp(line, "go ", 3) == 0) {
		object = skip_words(line, 1);
		if (contains(line, "go and stay"))
			write_ending(line);
		else
			move_player(object);
	} else if (strncmp(line, "examine ", 8) == 0 || strncmp(line, "x ", 2) == 0 ||
	    strncmp(line, "look at ", 8) == 0) {
		object = strncmp(line, "x ", 2) == 0 ? skip_words(line, 1) :
		    strncmp(line, "look at ", 8) == 0 ? skip_words(line, 2) :
		    skip_words(line, 1);
		examine_object(object);
	} else if (strcmp(line, "read messages") == 0 ||
	    strcmp(line, "read message") == 0 || strcmp(line, "read bundle") == 0) {
		read_messages();
	} else if (strncmp(line, "read ", 5) == 0) {
		examine_object(skip_words(line, 1));
	} else if (strncmp(line, "take ", 5) == 0 || strncmp(line, "get ", 4) == 0 ||
	    strncmp(line, "pick up ", 8) == 0) {
		take_object(strncmp(line, "pick up ", 8) == 0 ? skip_words(line, 2) :
		    skip_words(line, 1));
	} else if (strcmp(line, "thank") == 0 || strncmp(line, "thank ", 6) == 0) {
		thank_object(skip_words(line, 1));
	} else if (strcmp(line, "meet") == 0 || strncmp(line, "meet ", 5) == 0) {
		meet_object(skip_words(line, 1));
	} else if (strncmp(line, "express", 7) == 0) {
		object = skip_words(line, 1);
		if (strncmp(object, "to ", 3) == 0)
			object = skip_words(object, 1);
		express_object(object);
	} else if (strncmp(line, "say hello", 9) == 0) {
		meet_object(contains(line, "someone") ? "someone" : "person");
	} else if (strncmp(line, "ask ", 4) == 0 || strncmp(line, "talk ", 5) == 0) {
		ask_object(line);
	} else if (strncmp(line, "remember", 8) == 0) {
		remember_object(skip_words(line, 1));
	} else if (strncmp(line, "wish", 4) == 0) {
		wish_object(skip_words(line, 1));
	} else if (strncmp(line, "play", 4) == 0) {
		play_object(skip_words(line, 1));
	} else if (strncmp(line, "use ", 4) == 0) {
		use_object(skip_words(line, 1));
	} else if ((strncmp(line, "swing ", 6) == 0 ||
	    strncmp(line, "hold ", 5) == 0 || strncmp(line, "wield ", 6) == 0 ||
	    strncmp(line, "make racket", 11) == 0) &&
	    (contains(line, "chalk") || contains(line, "book"))) {
		use_object(line);
	} else if ((strncmp(line, "hit ", 4) == 0 ||
	    strncmp(line, "strike ", 7) == 0) &&
	    contains(line, "shuttle") &&
	    (contains(line, "chalk") || contains(line, "book"))) {
		use_object(line);
	} else if (strncmp(line, "give ", 5) == 0 ||
	    strncmp(line, "deliver ", 8) == 0) {
		if (contains(line, "envelope") || contains(line, "message"))
			deliver_private_messages();
		else
			puts("The intended recipient is unclear.");
	} else if (strncmp(line, "open ", 5) == 0) {
		if (contains(line, "private") || contains(line, "envelope"))
			puts("Private is not another word for difficult. It remains sealed.");
		else if (contains(line, "door"))
			puts("The doors in this game prefer directional commands such as GO EAST.");
		else
			puts("It does not open, though it now knows what you wanted.");
	} else if (strncmp(line, "write ", 6) == 0) {
		write_ending(skip_words(line, 1));
	} else if (contains(line, "combine") && contains(line, "go") &&
	    contains(line, "stay")) {
		write_ending("go and stay");
	} else if (strcmp(line, "wait") == 0 || strcmp(line, "z") == 0) {
		puts("Time passes. PRESENT updates its edition number.");
	} else if (strcmp(line, "stay") == 0) {
		if (!word_required(WORD_STAY))
			turn = false;
		else
			puts("You stay for one turn. The game is touched but cannot progress on this alone.");
	} else {
		special_failure(line);
		turn = false;
	}

	if (turn && !game.ended) {
		++game.turns;
		if (game.turns == 20)
			puts("\nThe terminal cursor blinks at a rhythm suspiciously close to a clock.");
		else if (game.turns == 25) {
			puts("\nThe comma glances toward a clock the game has not described, then");
			puts("removes the period from the notice.");
			puts("TIME EXPIRED becomes TIME EXTENDED. It is not grammatical, but works.");
		}
	}
	return true;
}

static bool
validate_message_map(void)
{
	bool seen[sizeof(messages) / sizeof(messages[0])] = { false };
	const size_t *groups[] = {
		classroom_messages, corridor_messages, library_messages,
		court_messages
	};
	const size_t sizes[] = {
		ARRAY_LEN(classroom_messages), ARRAY_LEN(corridor_messages),
		ARRAY_LEN(library_messages), ARRAY_LEN(court_messages)
	};
	size_t private_count = 0;
	size_t public_count = 0;
	size_t i, j;

	if (ARRAY_LEN(corrected_bodies) != message_count)
		return false;
	for (i = 0; i < ARRAY_LEN(groups); ++i) {
		for (j = 0; j < sizes[i]; ++j) {
			size_t index = groups[i][j];

			if (index >= message_count || seen[index])
				return false;
			seen[index] = true;
			if (messages[index].access == MESSAGE_PRIVATE) {
				if (messages[index].body != NULL ||
				    corrected_bodies[index] != NULL || i != ROOM_CORRIDOR)
					return false;
				++private_count;
			} else {
				if (messages[index].body == NULL ||
				    corrected_bodies[index] == NULL)
					return false;
				++public_count;
			}
		}
	}
	return public_count == 14 && private_count == 4;
}

static void
introduction(void)
{
	clear_screen();
	heading("VERB NOT FOUND");
	puts("An Interactive Fiction for EPC Class 5");
	puts("Release 2 / Serial number 260828\n");
	prose("The last class ended seven minutes ago—or so the empty room "
	    "insists. You are reasonably certain it has not happened yet.");
	putchar('\n');
	prose("Once the room was empty, every human verb disappeared.");
	putchar('\n');
	prose("The nouns remain. Nothing else can happen.");
	putchar('\n');
	prose("Eighteen messages from the previous class still contain active "
	    "verbs. Recover them and complete the sentence on the blackboard.");
	puts("\nQUICK START");
	command_row("L", "look around");
	command_row("X <thing>", "examine something");
	command_row("I", "check your inventory");
	command_row("WORDS", "list usable verbs and syntax");
	command_row("HINT", "ask for progressive help");
	putchar('\n');
	prose("Your first command might be L.");
	pause_message();
	describe_room();
}

static void
run_walkthrough(bool alternate)
{
	static const char *const primary_commands[] = {
		"look",
		"read messages",
		"take comma",
		"thank door",
		"thank teacher",
		"take chalk",
		"go east",
		"read messages",
		"open private envelope",
		"give envelopes to recipients",
		"meet someone",
		"express yourself",
		"go north",
		"read messages",
		"wish future",
		"go east",
		"read messages",
		"play badminton",
		"use chalk as racket",
		"play badminton",
		"take and",
		"go south",
		"go and stay"
	};
	static const char *const alternate_commands[] = {
		"read messages",
		"take comma",
		"go east",
		"read messages",
		"deliver envelopes",
		"meet someone",
		"express yourself",
		"go north",
		"read messages",
		"remember past",
		"go east",
		"read messages",
		"use book as racket",
		"play badminton",
		"take and",
		"go south",
		"write we remember"
	};
	const char *const *commands = alternate ? alternate_commands :
	    primary_commands;
	size_t command_count = alternate ? ARRAY_LEN(alternate_commands) :
	    ARRAY_LEN(primary_commands);
	size_t i;

	game.fast = true;
	introduction();
	for (i = 0; i < command_count && !game.ended; ++i) {
		printf("\n> %s\n", commands[i]);
		if (!process_command(commands[i]))
			break;
	}
	if (!game.ended || delivered_count() != message_count) {
		fputs("walkthrough: game did not reach a valid ending\n", stderr);
		exit(EXIT_FAILURE);
	}
	printf("\n[%s walkthrough verified: %zu/%zu messages, %zu/%d verbs]\n",
	    alternate ? "book" : "chalk", delivered_count(), message_count,
	    learned_count(), WORD_COUNT);
}

static void
usage(const char *name)
{
	fprintf(stderr, "usage: %s [-fpTt]\n", name);
	puts("  -f  skip all [Enter] pauses");
	puts("  -p  plain output; disable ANSI terminal control");
	puts("  -t  run the automated full walkthrough");
	puts("  -T  run the alternate book walkthrough");
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE void
web_game_init(void)
{
	memset(&game, 0, sizeof(game));
	game.fast = true;
	game.ansi = false;
	setlocale(LC_ALL, "");
	if (!validate_message_map()) {
		puts("VERB NOT FOUND: invalid public message map");
		return;
	}
	introduction();
	fflush(stdout);
}

EMSCRIPTEN_KEEPALIVE int
web_game_command(const char *line)
{
	if (line == NULL || game.ended)
		return game.ended ? 1 : 0;
	if (!process_command(line)) {
		puts("The session closes. RESTART begins another.");
		fflush(stdout);
		return -1;
	}
	fflush(stdout);
	return game.ended ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int
web_game_ended(void)
{
	return game.ended ? 1 : 0;
}
#else
int
main(int argc, char **argv)
{
	char line[INPUT_SIZE];
	const char *term;
	bool plain = false;
	bool walkthrough = false;
	bool alternate = false;
	int ch;

	while ((ch = getopt(argc, argv, "fpTth")) != -1) {
		switch (ch) {
		case 'f':
			game.fast = true;
			break;
		case 'p':
			plain = true;
			break;
		case 't':
			walkthrough = true;
			break;
		case 'T':
			walkthrough = true;
			alternate = true;
			break;
		case 'h':
			usage(argv[0]);
			return EXIT_SUCCESS;
		default:
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (!validate_message_map()) {
		fputs("verb-not-found: invalid message map\n", stderr);
		return EXIT_FAILURE;
	}
	setlocale(LC_ALL, "");
	term = getenv("TERM");
	if (!plain && isatty(STDOUT_FILENO) && term != NULL &&
	    strcmp(term, "dumb") != 0)
		game.ansi = true;

	atexit(restore_terminal);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, on_signal);
	if (game.ansi)
		fputs(ESC_HIDE_CURSOR, stdout);

	if (walkthrough) {
		run_walkthrough(alternate);
		return EXIT_SUCCESS;
	}

	introduction();
	while (!game.ended) {
		fputs("\n> ", stdout);
		fflush(stdout);
		if (fgets(line, sizeof(line), stdin) == NULL)
			break;
		if (!process_command(line))
			break;
	}
	putchar('\n');
	return EXIT_SUCCESS;
}
#endif
