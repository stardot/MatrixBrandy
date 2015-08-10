/*
** This file is part of the Brandy Basic V Interpreter.
** Copyright (C) 2000, 2001, 2002, 2003, 2004 David Daniels
**
** Brandy is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2, or (at your option)
** any later version.
**
** Brandy is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with Brandy; see the file COPYING.  If not, write to
** the Free Software Foundation, 59 Temple Place - Suite 330,
** Boston, MA 02111-1307, USA.
**
**
**	This file contains functions that convert numbers between
**	character and binary format
*/

#include <ctype.h>
#include <math.h>
#include "common.h"
#include "target.h"
#include "convert.h"
#include "errors.h"
#include "miscprocs.h"

/*
** 'todigit' converts the character 'x' to its numeric equivalent
*/
int todigit(char x) {
  if (x>='0' && x<='9') return x-'0';
  if (x>='A' && x<='F') return x-'A'+10;
  if (x>='a' && x<='f') return x-'a'+10;
  return 0;
}

#define INTCONV (MAXINTVAL/10)

/*
** 'tonumber' converts the character string starting at 'cp' to binary.
** It handles integer and floating point values, including numbers
** expressed in hexadecimal and binary. It returns a pointer to the
** character after the last one used in the number or 'NIL' if an error
** was detected. The value is returned at either 'floatvalue' or
** 'intvalue' depending on the type of the number. 'isinteger' says
** which it is. In the event of an error, 'intvalue' is used to return
** an error number
*/
char *tonumber(char *cp, boolean *isinteger, int32 *intvalue, float64 *floatvalue) {
  int32 value;
  static float64 fpvalue, fltdiv;
  boolean isint, isneg, negexp;
  int digits, exponent;
  value = 0;
  digits = 0;
  cp = skip_blanks(cp);	/* Ignore leading white space characters */
  switch (*cp) {
  case '&':	/* Hex value */
    cp++;
    while (isxdigit(*cp)) {
      digits++;
      value = (value<<4)+todigit(*cp);
      cp++;
    }
    if (digits==0) {
      *intvalue = WARN_BADHEX;	/* Bad hexadecimal constant */
      cp = NIL;
    }
    else {
      *intvalue = value;
      *isinteger = TRUE;
    }
    break;
  case '%':	/* Binary value */
    cp++;
    while (*cp=='0' || *cp=='1') {
      digits++;
      value = (value<<1)+(*cp-'0');
      cp++;
    }
    if (digits==0) {
      *intvalue = WARN_BADBIN;	/* Bad binary constant */
      cp = NIL;
    }
    else {
      *intvalue = value;
      *isinteger = TRUE;
    }
    break;
  default:	/* Integer or floating point value */
    isint = TRUE;
    isneg = *cp=='-';	/* Deal with any sign first */
    if (*cp=='+' || *cp=='-') cp++;
    while (*cp>='0' && *cp<='9') {
      digits = 0;	/* Used to count the number of digits before the '.' */
      if (isint && value>=INTCONV) {
        isint = FALSE;
        fpvalue = TOFLOAT(value);
      }
      if (isint)
        value = value*10+(*cp-'0');
      else {
        fpvalue = fpvalue*10.0+TOFLOAT(*cp-'0');
      }
      digits++;
      cp++;
    }
    if (!isint && *cp!='.' && *cp!='E' && fpvalue<=TOFLOAT(MAXINTVAL)) {	/* Convert back to integer */
      value = TOINT(fpvalue);
      isint = TRUE;
    }
    if (*cp=='.') {	/* Number contains a decimal point */
      if (isint) {
        isint = FALSE;
        fpvalue = TOFLOAT(value);
      }
      fltdiv = 1.0;
      cp++;
      while (*cp>='0' && *cp<='9') {
        fpvalue = fpvalue*10.0+TOFLOAT(*cp-'0');
        fltdiv = fltdiv*10.0;
        cp++;
      }
      fpvalue = fpvalue/fltdiv;
    }
/*
** Deal with an exponent. Note one trick here: if the 'E' is followed by another
** letter it is assumed that the 'E' is part of a word that follows the number,
** that is, there is not really an exponent here
*/
    if (toupper(*cp)=='E' && !isalpha(*(cp+1))) {	/* Number contains an exponent */
      if (isint) {
        isint = FALSE;
        fpvalue = value;
      }
      exponent = 0;
      cp++;
      negexp = *cp=='-';
      if (*cp=='+' || *cp=='-') cp++;
      while (*cp>='0' && *cp<='9' && exponent<=MAXEXPONENT) {
        exponent = exponent*10+(*cp-'0');
        cp++;
      }
      if (negexp) {
        if (exponent-digits<=MAXEXPONENT)
          exponent = -exponent;
        else {	/* If value<1E-308, set value to 0 */
          exponent = 0;
          fpvalue = 0;
        }
      }
      else if (exponent+digits-1>MAXEXPONENT) {	/* Crude check for overflow on +ve exponent */
        *intvalue = WARN_EXPOFLO;
        cp = NIL;
        exponent = 0;
      }
      fpvalue = fpvalue*pow(10.0, exponent);
    }
    *isinteger = isint;
    if (isint)
      *intvalue = (isneg ? -value : value);
    else {
      *floatvalue = (isneg ? -fpvalue : fpvalue);
    }
  }
  return cp;
}

/*
** 'itob' formats the value 'value' as a binary number at 'dest'.
** It returns a count of the number of characters in the formatted
** number. 'width' gives the minimum field width, but this is not
** used at present
*/
int itob(char *dest, int32 value, int32 width) {
  int count, n;
  char temp[sizeof(int32)*8];
  for (n=0; n<sizeof(int32)*8; n++) {
    temp[n] = (value & 1)+'0';
    value = value>>1;
  }
  n = sizeof(int32)*8-1;
  while (n>0 && temp[n]=='0') n--;
  count = n+1;
  while (n>=0) {
    *dest = temp[n];
    dest++;
    n--;
  }
  *dest = NUL;
  return count;
}

