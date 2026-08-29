#ifndef MESSAGES_H
#define MESSAGES_H

#include <stddef.h>

enum message_access {
	MESSAGE_PUBLIC,
	MESSAGE_PRIVATE
};

struct message {
	unsigned int source_id;
	enum message_access access;
	const char *from;
	const char *to;
	const char *body;
};

/*
 * Questionnaire snapshot: 2026-08-27
 *
 * 19 CSV records were reviewed.  Record 1 (test/Test2/Test3) is an
 * obvious form test and is intentionally excluded from the show.
 *
 * Public text is kept as submitted, including its original spelling and
 * punctuation.  Private bodies and recipient identities are deliberately
 * absent.  The source questionnaire must never be added to this public repo.
 */
static const struct message messages[] = {
	{
		19, MESSAGE_PUBLIC, "anonymous", "Bobbie Wang and EPC Class 5",
		"To teacher Wang， I’d like to say：I’m truly grateful to have a "
		"teacher as excellent as you. You are adept at discovering every "
		"student’s bright spots， and excel at guiding and inspiring us. I "
		"look forward to having your classes later on. May everything go "
		"well with your work and may you be happy every single day.\n\n"
		"And to the whole epc 5 class，It’s a pleasure to get to know all "
		"of you. You’ve made the past two weeks of learning fun and "
		"fulfilling. May each of us reach our goals and fulfill our dreams "
		"in the days ahead."
	},
	{
		18, MESSAGE_PRIVATE, "hidden", "a private recipient", NULL
	},
	{
		17, MESSAGE_PUBLIC, "anonymous", "EPC Class 5",
		"It's a very precious memory for me.\nThankyou Bobbie ！！！"
	},
	{
		16, MESSAGE_PUBLIC, "anonymous", "Bobbie and EPC Class 5",
		"Bobby is a wonderful ， patient and skillful teacher ! I feel so "
		"lucky to meet her in the beginning of my university journey ! "
		"And I like all my classmates ! ❤️ All of you are kind and "
		"easygoing! Hope all of you a colorful university lives!"
	},
	{
		15, MESSAGE_PUBLIC, "anonymous",
		"Bobbie, the peer leader, and EPC Class 5",
		"Thanks all of our classmates and our peer leader，thanks Bobbie，"
		"let us study in the fun，make new friends，and have a head start "
		"in the university life"
	},
	{
		14, MESSAGE_PUBLIC, "anonymous",
		"Bobbie, the peer leader, and EPC Class 5",
		"Hi Bobbie，hi peer leader，hi my classmates. I am really really "
		"glad to have met you all and had a wonderful and unforgettable "
		"epc course.Wish all of us be happy every day and realize our "
		"dreams.See you！"
	},
	{
		13, MESSAGE_PUBLIC, "Z.Z.L.", "EPC Class 5",
		"Thanks for company thankd for laerning together，for not just "
		"helping me integrate into the new enviroment but let me knew "
		"someone who i can emphasize. Thank you all!"
	},
	{
		12, MESSAGE_PUBLIC, "someone who treasures this time",
		"all my cute classmates",
		"Enjoying the time with you all， and thank you for making me a "
		"better person"
	},
	{
		11, MESSAGE_PUBLIC, "Xiyi", "Bobbie Wang",
		"Thankyou for your support and guaidance.Because of you I have "
		"courage to express myslef even though I have some in pronunciation "
		"and listening.Thankyou very much!"
	},
	{
		10, MESSAGE_PRIVATE, "hidden", "a private recipient", NULL
	},
	{
		9, MESSAGE_PUBLIC, "anonymous", "Bobbie Wang",
		"Thank for your guide of the spoken English to me"
	},
	{
		8, MESSAGE_PUBLIC, "anonymous", "Bobbie Wang and EPC Class 5",
		"congratulations！And appreciate for Miss.Bobbie’s company"
	},
	{
		7, MESSAGE_PUBLIC, "anonymous", "EPC Class 5",
		"I love you all🥹🫶🏻an unforgettable memory in my mind forever "
		"😸🫰🏻"
	},
	{
		6, MESSAGE_PUBLIC, "anonymous", "D.X.T.",
		"明天下午打不打羽毛球"
	},
	{
		5, MESSAGE_PRIVATE, "hidden", "a private recipient", NULL
	},
	{
		4, MESSAGE_PUBLIC, "anonymous", "EPC Class 5",
		"Enjoy the days spent with you guys. Let’s stay in touch!"
	},
	{
		3, MESSAGE_PRIVATE, "hidden", "a private recipient", NULL
	},
	{
		2, MESSAGE_PUBLIC, "anonymous", "EPC Class 5",
		"祝大家在新的学习旅程中越来越好！美团老鼠将陪伴你们的欢乐～"
	}
};

static const size_t message_count =
	sizeof(messages) / sizeof(messages[0]);

#endif
