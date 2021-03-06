/* radare - LGPL - Copyright 2009-2016 - pancake, nibble */

#include <r_cons.h>
#include <r_util.h>
#define sdb_json_indent r_cons_json_indent
#define sdb_json_unindent r_cons_json_unindent
#include "../../shlr/sdb/src/json/indent.c"

/* TODO: remove globals */
static RList *sorted_lines = NULL;
static RList *unsorted_lines = NULL;
static int sorted_column = -1;

R_API void r_cons_grep_help() {
	eprintf (
"|Usage: [command]~[modifier][word,word][endmodifier][[column]][:line]\n"
"| modifiers:\n"
"|   &        all words must match to grep the line\n"
"|   $[n]     sort numerically / alphabetically the Nth column\n"
"|   ^        words must be placed at the beginning of line\n"
"|   !        negate grep\n"
"|   ?        count number of matching lines\n"
"|   ..       internal 'less'\n"
"|   {}       json indentation\n"
"|   {}..     less json indentation\n"
"| endmodifiers:\n"
"|   $        words must be placed at the end of line\n"
"| column:\n"
"|   [n]      show only column n\n"
"|   [n-m]    show column n to m\n"
"|   [n-]     show all columns starting from column n\n"
"| examples:\n"
"|   i~:0     show fist line of 'i' output\n"
"|   pd~mov   disasm and grep for mov\n"
"|   pi~[0]   show only opcode\n"
"|   i~0x400$ show lines ending with 0x400\n"
	);
}

#define R_CONS_GREP_BUFSIZE 4096

R_API void r_cons_grep(const char *str) {
	int wlen, len, is_range, num_is_parsed, fail = 0;
	ut64 range_begin, range_end;
	RCons *cons;
	char buf[R_CONS_GREP_BUFSIZE];
	char *ptr, *optr, *ptr2, *ptr3;

	if (!str || !*str) {
		return;
	}
	cons = r_cons_singleton ();
	memset (&(cons->grep), 0, sizeof (cons->grep));
	sorted_column = 0;
	cons->grep.sort = -1;
	cons->grep.line = -1;
	while (*str) {
		switch (*str) {
		case '.':
			if (str[1] == '.') {
				cons->grep.less = 1;
				return;
			}
			str++;
			break;
		case '{':
			if (str[1] == '}') {
				cons->grep.json = 1;
				if (!strncmp (str, "{}..", 4)) {
					cons->grep.less = 1;
				}
				str++;
				return;
			}
			str++;
			break;
		case '$':
			str++;
			if (*str == '!') {
				cons->grep.sort_invert = true;
				str++;
			} else {
				cons->grep.sort_invert = false;
			}
			cons->grep.sort = atoi (str);
			while (IS_NUMBER (*str)) {
				str++;
			}
			if (*str == ':') {
				cons->grep.sort_row = atoi (++str);
				str++;
			}
			break;
		case '&':
			str++;
			cons->grep.amp = 1;
			break;
		case '^':
			str++;
			cons->grep.begin = 1;
			break;
		case '!':
			str++;
			cons->grep.neg = 1;
			break;
		case '?': str++; cons->grep.counter = 1;
			if (*str == '?') {
				r_cons_grep_help ();
				return;
			}
			break;
		default:
			goto while_end;
		}
	}
	while_end:

	len = strlen (str) - 1;
	if (len > R_CONS_GREP_BUFSIZE - 1) {
		eprintf("r_cons_grep: too long!\n");
		return;
	}
	if (len > 0 && str[len] == '?') {
		cons->grep.counter = 1;
		strncpy (buf, str, R_MIN (len, sizeof (buf) - 1));
		buf[len]=0;
		len--;
	} else {
		strncpy (buf, str, sizeof (buf) - 1);
	}

	if (len > 1 && buf[len] == '$' && buf[len - 1] != '\\') {
		cons->grep.end = 1;
		buf[len] = 0;
	}

	ptr = buf;
	ptr2 = strchr (ptr, '[');
	ptr3 = strchr (ptr, ']');
	is_range = 0;
	num_is_parsed = 0;
	fail = 0;
	range_begin = range_end = -1;

	if (ptr2 && ptr3) {
		ptr2[0] = '\0';
		ptr2++;
		for (; ptr2 <= ptr3; ++ptr2) {
			if (fail) {
				memset (cons->grep.tokens, 0, R_CONS_GREP_TOKENS);
				cons->grep.tokens_used = 0;
				fail = 0;
				break;
			}
			switch (*ptr2) {
			case '-':
				is_range = 1;
				num_is_parsed = 0;
				break;
			case ']':  // fallthrough to handle ']' like ','
			case ',':
				for (; range_begin <= range_end; range_begin++) {
					if (range_begin >= R_CONS_GREP_TOKENS) {
						fail = 1;
						break;
					}
					cons->grep.tokens[range_begin] = 1;
					cons->grep.tokens_used = 1;
				}
				is_range = 0;
				num_is_parsed = 0;
				break;
			default:
				if (!num_is_parsed) {
					if (is_range) {
						range_end = r_num_get (cons->num, ptr2);
					} else {
						range_begin = range_end = r_num_get (cons->num, ptr2);
					}
					num_is_parsed = 1;
				}
			}
		}
	}

	ptr2 = strchr (ptr, ':'); // line number
	cons->grep.range_line = 2; //there is not :
	if (ptr2 && ptr2[1] != ':') {
		*ptr2 = '\0';
		char *p, *token = ptr + 1;
		p = strstr (token, "..");
		if (!p) {
			cons->grep.line = r_num_get (cons->num, ptr2 + 1);
			cons->grep.range_line = 0;
		} else {
			*p = '\0';
			cons->grep.range_line = 1;
			if (!*token) {
				cons->grep.f_line = 0;
			} else {
				cons->grep.f_line = r_num_get (cons->num, token);
			}
			if (!p[2]) {
				cons->grep.l_line = -1;
			} else  {
				cons->grep.l_line = r_num_get (cons->num, p + 2);
			}
		}
	}
	free (cons->grep.str);
	if (*ptr) {
		cons->grep.str = (char *)strdup (ptr);
		do {
			optr = ptr;
			ptr = strchr (ptr, ','); // grep keywords
			if (ptr) {
				*ptr++ = '\0';
			}
			wlen = strlen (optr);
			if (!wlen) {
				continue;
			}
			if (wlen >= R_CONS_GREP_WORD_SIZE - 1) {
				eprintf ("grep string too long\n");
				continue;
			}
			strncpy (cons->grep.strings[cons->grep.nstrings],
				optr, R_CONS_GREP_WORD_SIZE - 1);
			cons->grep.nstrings++;
			if (cons->grep.nstrings > R_CONS_GREP_WORDS - 1) {
				eprintf ("too many grep strings\n");
				break;
			}
		} while (ptr);
	} else {
		cons->grep.str = strdup (ptr);
		cons->grep.nstrings++;
		cons->grep.strings[0][0] = 0;
	}
}

static int cmp (const void *a, const void *b) {
	char *da = NULL;
	char *db = NULL;
	const char *ca = r_str_chop_ro (a);
	const char *cb = r_str_chop_ro (b);
	if (!a || !b) {
		return (int)(size_t)(a - b);
	}
	if (sorted_column > 0) {
		da = strdup (ca);
		db = strdup (cb);
		int colsa = r_str_word_set0 (da);
		int colsb = r_str_word_set0 (db);
		ca = (colsa > sorted_column) ? r_str_word_get0 (da, sorted_column): "";
		cb = (colsb > sorted_column) ? r_str_word_get0 (db, sorted_column): "";
	}
	if (IS_NUMBER (*ca) && IS_NUMBER (*cb)) {
		ut64 na = r_num_get (NULL, ca);
		ut64 nb = r_num_get (NULL, cb);
		int ret = na > nb;
		free (da);
		free (db);
		return ret;
	}
	if (da && db) {
		int ret = strcmp (ca, cb);
		free (da);
		free (db);
		return ret;
	}
	free (da);
	free (db);
	return strcmp (a, b);
}

R_API int r_cons_grepbuf(char *buf, int len) {
	RCons *cons = r_cons_singleton ();
	char *tline, *tbuf, *p, *out, *in = buf;
	int ret, total_lines = 0, buffer_len = 0, l = 0, tl = 0;
	bool show = false;

	if ((!len || !buf || buf[0] == '\0') &&
	   (cons->grep.json || cons->grep.less)) {
		cons->grep.json = 0;
		cons->grep.less = 0;
		return 0;
	}
	if (cons->grep.json) {
		char *out = sdb_json_indent (buf);
		free (cons->buffer);
		cons->buffer = out;
		cons->buffer_len = strlen (out);
		cons->buffer_sz = cons->buffer_len + 1;
		cons->grep.json = 0;
		if (cons->grep.less) {
			cons->grep.less = 0;
			r_cons_less_str (cons->buffer, NULL);
		}
		return 3;
	}
	if (cons->grep.less) {
		cons->grep.less = 0;
		r_cons_less_str (buf, NULL);
		buf[0] = 0;
		cons->buffer_len = 0;
		if (cons->buffer) {
			cons->buffer[0] = 0;
		}
		free (cons->buffer);
		cons->buffer = NULL;
		return 0;
	}
	if (!cons->buffer) {
		cons->buffer_len = len + 20;
		cons->buffer = malloc (cons->buffer_len);
		cons->buffer[0] = 0;
	}
	out = tbuf = calloc (1, len);
	tline = malloc (len);
	cons->lines = 0;
	//used to count lines and change negative grep.line values
	while ((int)(size_t)(in-buf) < len) {
		p = strchr (in, '\n');
		if (!p) {
			break;
		}
		l = p - in;
		if (l > 0) {
			in += l + 1;
		} else {
			in++;
		}
		total_lines++;
	}
	if (!cons->grep.range_line && cons->grep.line < 0) {
		cons->grep.line = total_lines + cons->grep.line;
	}
	if (cons->grep.range_line == 1) {
		if (cons->grep.f_line < 0) {
			cons->grep.f_line = total_lines + cons->grep.f_line;
		}
		if (cons->grep.l_line < 0) {
			cons->grep.l_line = total_lines + cons->grep.l_line;
		}
	}
	in = buf;
	while ((int)(size_t)(in-buf) < len) {
		p = strchr (in, '\n');
		if (!p) {
			free (tbuf);
			free (tline);
			return 0;
		}
		l = p - in;
		if (l > 0) {
			memcpy (tline, in, l);
			tl = r_str_ansi_filter (tline, NULL, NULL, l);
			if (tl < 0) {
				ret = -1;
			} else {
				ret = r_cons_grep_line (tline, tl);
				if (!cons->grep.range_line) {
					if (cons->grep.line == cons->lines) {
						show = true;
					}
				} else if (cons->grep.range_line == 1) {
					if (cons->grep.f_line == cons->lines) {
						show = true;
					}
					if (cons->grep.l_line == cons->lines) {
						show = false;
					}
				} else {
					show = true;
				}
			}
			if (ret > 0) {
				if (show) {
					memcpy (out, tline, ret);
					memcpy (out + ret, "\n", 1);
					out += ret + 1;
					buffer_len += ret + 1;
				}
				if (!cons->grep.range_line) {
					show = false;
				}
				cons->lines++;
			} else if (ret < 0) {
				free (tbuf);
				free (tline);
				return 0;
			}
			in += l + 1;
		} else {
			in++;
		}
	}
	memcpy (buf, tbuf, len);
	cons->buffer_len = buffer_len;
	free (tbuf);
	free (tline);
	if (cons->grep.counter) {
		if (cons->buffer_len < 10) {
			cons->buffer_len = 10; // HACK
		}
		snprintf (cons->buffer, cons->buffer_len, "%d\n", cons->lines);
		cons->buffer_len = strlen (cons->buffer);
		cons->num->value = cons->lines;
	}
	if (cons->grep.sort != -1) {
#define INSERT_LINES(list) \
	do {\
		r_list_foreach (list, iter, str) { \
			int len = strlen (str);\
			memcpy (ptr, str, len);\
			memcpy (ptr + len, "\n", 2); \
			ptr += len + 1; \
			nl++; \
		} \
	} while (false)

		RListIter *iter;
		int nl = 0;
		char *ptr = cons->buffer;
		char *str;
		sorted_column = cons->grep.sort;
		r_list_sort (sorted_lines, cmp);
		if (cons->grep.sort_invert) {
			r_list_reverse (sorted_lines);
		}
		INSERT_LINES (unsorted_lines);
		INSERT_LINES (sorted_lines);
		cons->lines = nl;
		r_list_free (sorted_lines);
		sorted_lines = NULL;
		r_list_free (unsorted_lines);
		unsorted_lines = NULL;
	}
	return cons->lines;
}

R_API int r_cons_grep_line(char *buf, int len) {
	RCons *cons = r_cons_singleton ();
	const char *delims = " |,;=\t";
	char *in, *out, *tok = NULL;
	int hit = cons->grep.neg;
	int outlen = 0;
	bool use_tok = false;
	size_t i;

	in = calloc (1, len + 1);
	if (!in) {
		return 0;
	}
	out = calloc (1, len + 2);
	if (!out) {
		free (in);
		return 0;
	}
	memcpy (in, buf, len);

	if (cons->grep.nstrings > 0) {
		int ampfail = cons->grep.amp;
		for (i = 0; i < cons->grep.nstrings; i++) {
			char *p = strstr (in, cons->grep.strings[i]);
			if (!p) {
				ampfail = 0;
				continue;
			}
			if (cons->grep.begin) {	
				hit = (p == in) ? 1 : 0;
			} else {
				hit = !cons->grep.neg;
			}
			// TODO: optimize without strlen without breaking t/feat_grep (grep end)
			if (cons->grep.end && (strlen (cons->grep.strings[i]) != strlen (p))) {
				hit = 0 ;
			}
			if (!cons->grep.amp) {
				break;
			}
		}
		if (cons->grep.amp) {
			hit = ampfail;
		}
	} else {
		hit = 1;
	}

	if (hit) {
		if (!cons->grep.range_line) {
			if (cons->grep.line == cons->lines) {
				use_tok = true;
			}
		} else if (cons->grep.range_line == 1) {
			if (cons->grep.f_line == cons->lines) {
				use_tok = true;
			}
			if (cons->grep.l_line == cons->lines) {
				use_tok = false;
			}
		} else {
			use_tok = true;
		}
		if (use_tok && cons->grep.tokens_used) {
			for (i = 0; i < R_CONS_GREP_TOKENS; i++) {
				tok = strtok (i ? NULL : in, delims);

				if (tok) {
					if (cons->grep.tokens[i]) {
						int toklen = strlen (tok);
						memcpy (out + outlen, tok, toklen);
						memcpy (out + outlen + toklen, " ", 2);
						outlen += toklen + 1;
						if (!(*out)) {
							free (in);
							free (out);
							return -1;
						}
					}
				} else {
					if (!(*out)) {
						free (in);
						free (out);
						return -1;
					} else {
						break;
					}
				}
			}

			outlen = outlen > 0 ? outlen - 1 : 0;
			if (outlen > len) { // should never happen
				eprintf ("r_cons_grep_line: wtf, how you reach this?\n");
				free (in);
				free (out);
				return -1;
			}

			memcpy (buf, out, len);
			len = outlen;
		}
	} else {
		len = 0;
	}
	free (in);
	free (out);
	if (cons->grep.sort != -1) {
		char ch = buf[len];
		buf[len] = 0;
		if (!sorted_lines) {
			sorted_lines = r_list_newf (free);
		}
		if (!unsorted_lines) {
			unsorted_lines = r_list_newf (free);
		}
		if  (cons->lines > cons->grep.sort_row) {
			r_list_append (sorted_lines, strdup (buf));
		} else {
			r_list_append (unsorted_lines, strdup (buf));
		}
		buf[len] = ch;
	}

	return len;
}

static const char *gethtmlrgb(const char *str) {
	static char buf[32];
	ut8 r, g, b;
	r = g = b = 0;
	r_cons_rgb_parse (str, &r, &g, &b, 0);
	sprintf (buf, "#%02x%02x%02x", r, g, b);
	return buf;
}

static const char *gethtmlcolor(const char ptrch, const char *def) {
	switch (ptrch) {
	case '0': return "#000"; // BLACK
	case '1': return "#f00"; // RED
	case '2': return "#0f0"; // GREEN
	case '3': return "#ff0"; // YELLOW
	case '4': return "#00f"; // BLUE
	case '5': return "#f0f"; // MAGENTA
	case '6': return "#aaf"; // TURQOISE
	case '7': return "#fff"; // WHITE
	case '8': return "#777"; // GREY
	case '9': break; // ???
	}
	return def;
}

// XXX: rename char *r_cons_filter_html(const char *ptr)
R_API int r_cons_html_print(const char *ptr) {
	const char *str = ptr;
	int esc = 0;
	int len = 0;
	int inv = 0;
	int tmp;
	bool tag_font = false;

	if (!ptr) {
		return 0;
	}
	for (;ptr[0]; ptr = ptr + 1) {
		if (0 && ptr[0] == '\n') {
			printf ("<br />");
			fflush (stdout);
		}
		if (ptr[0] == '<') {
			tmp = (int) (size_t) (ptr-str);
			if (write (1, str, tmp) != tmp) {
				eprintf ("r_cons_html_print: write: error\n");
			}
			printf ("&lt;");
			fflush (stdout);
			str = ptr + 1;
			continue;
		} else if (ptr[0] == '>') {
			tmp = (int) (size_t) (ptr-str);
			if (write (1, str, tmp) != tmp) {
				eprintf ("r_cons_html_print: write: error\n");
			}
			printf ("&gt;");
			fflush (stdout);
			str = ptr + 1;
			continue;
		}
		if (ptr[0] == 0x1b) {
			esc = 1;
			tmp = (int) (size_t) (ptr-str);
			if (write (1, str, tmp) != tmp) {
				eprintf ("r_cons_html_print: write: error\n");
			}
			if (tag_font) {
				printf ("</font>");
				fflush (stdout);
				tag_font = false;
			}
			str = ptr + 1;
			continue;
		}
		if (esc == 1) {
			// \x1b[2J
			if (ptr[0] != '[') {
				eprintf ("Oops invalid escape char\n");
				esc = 0;
				str = ptr + 1;
				continue;
			}
			esc = 2;
			continue;
		} else if (esc == 2) {
			// TODO: use dword comparison here
			if (ptr[0] == '2' && ptr[1] == 'J') {
				printf ("<hr />\n");
				fflush (stdout);
				ptr++;
				esc = 0;
				str = ptr;
				continue;
			} else if (!strncmp (ptr, "38;5;", 5)) {
				char *end = strchr (ptr, 'm');
				printf ("<font color='%s'>", gethtmlrgb (ptr));
				fflush (stdout);
				tag_font = true;
				ptr = end;
				str = ptr + 1;
				esc = 0;
			} else if (ptr[0] == '0' && ptr[1] == ';' && ptr[2] == '0') {
				r_cons_gotoxy (0, 0);
				ptr += 4;
				esc = 0;
				str = ptr;
				continue;
			} else if (ptr[0] == '0' && ptr[1] == 'm') {
				str = (++ptr) + 1;
				esc = inv = 0;
				continue;
				// reset color
			} else if (ptr[0] == '7' && ptr[1] == 'm') {
				str = (++ptr) + 1;
				inv = 128;
				esc = 0;
				continue;
				// reset color
			} else if (ptr[0] == '3' && ptr[2] == 'm') {
				printf ("<font color='%s'>", gethtmlcolor (ptr[1], inv ? "#fff" : "#000"));
				fflush (stdout);
				tag_font = true;
				ptr = ptr + 1;
				str = ptr + 2;
				esc = 0;
				continue;
			} else if (ptr[0] == '4' && ptr[2] == 'm') {
				printf ("<font style='background-color:%s'>",
						gethtmlcolor (ptr[1], inv ? "#000" : "#fff"));
				fflush (stdout);
				tag_font = true;
				ptr = ptr + 1;
				str = ptr + 2;
				esc = 0;
				continue;
			}
		}
		len++;
	}
	if (tag_font) {
		printf ("</font>");
		fflush (stdout);
		tag_font = false;
	}
	write (1, str, ptr - str);
	return len;
}
