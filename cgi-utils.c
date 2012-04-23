/*-
 * Copyright (c) 2011 Abhinav Upadhyay <er.abhinav.upadhyay@gmail.com>
 * All rights reserved.
 *
 * This code was developed as part of Google's Summer of Code 2011 program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <err.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Replaces all the occurrences of '+' in the given string with a space
 */
char *
parse_space(char *str)
{
	if (str == NULL)
		return str;

	char *iter = str;
	while ((iter = strchr(iter, '+')) != NULL)
		*iter = ' ';
	return str;
}

char *
parse_hex(char *str)
{
	char *percent_ptr;
	char hexcode[2];
	char *retval;
	retval = malloc(strlen(str) + 1);
	int i = 2;
	int decval = 0;
	size_t offset = 0;
	size_t sz;
	while ((percent_ptr = strchr(str, '%')) != NULL) {
		i = 2;
		sz = 0;
		sz = percent_ptr - str;
		decval = 0;
		memcpy(retval + offset, str, sz);
		percent_ptr++;
		offset += sz;
		while (i > 0) {
			switch (*percent_ptr) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				decval += (*percent_ptr - 48) * pow(16, --i);
				break;
			case 'A':
				decval += 10 * pow(16, --i);
				break;
			case 'B':
				decval += 11 * pow(16, --i);
				break;
			case 'C':
				decval += 12 * pow(16, --i);
				break;
			case 'D':
				decval += 13 * pow(16, --i);
				break;
			case 'E':
				decval += 14 * pow(16, --i);
				break;
			case 'F':
				decval += 15 * pow(16, --i);
				break;
			}
			percent_ptr++;
		}
		str = percent_ptr;
		retval[offset++] = decval;
	}
	sz =  strlen(str);
	memcpy(retval + offset, str, sz + 1);
	return retval;
}

/*
 * Parse the given query string to extract the parameter pname
 * and return to the caller. (Not the best way to do it but
 * going for simplicity at the moment.)
 */
char *
get_param(char *qstr, const char *pname)
{
	if (qstr == NULL)
		return NULL;

	char *temp;
	char *param = NULL;
	char *value = NULL;
	size_t sz;
	while (*qstr) {
		sz = strcspn(qstr, "=&");
		if (qstr[sz] == '=') {
			qstr[sz] = 0;
			param = qstr;
		}
		qstr += sz + 1;
		
		if (param && strcmp(param, pname) == 0)
			break;
		else
			param = NULL;
	}
	if (param == NULL) 
		return NULL;

	if ((temp = strchr(qstr, '&')) == NULL)
		value = strdup(qstr);
	else {
		sz = temp - qstr;
		value = malloc(sz + 1);
		if (value == NULL)
			errx(EXIT_FAILURE, "malloc failed");
		memcpy(value, qstr, sz);
		value[sz] = 0;
	}
	value = parse_space(value);
	char *retval = parse_hex(value);
	free(value);
	return retval;
}


