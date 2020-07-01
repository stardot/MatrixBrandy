/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2019 Michael McConnell and contributors
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
#define INT64CONV (MAXINT64VAL/10)

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
char *tonumber(char *cp, boolean *isinteger, int32 *intvalue, int64 *int64value, float64 *floatvalue) {
  int32 value;
  int64 value64;
  float64 fpvalue;
  boolean isint, isneg;
  int digits;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function convert.c:tonumber\n");
#endif
  value = 0;
  value64 = 0;
  fpvalue = 0;
  digits = 0;
  cp = skip_blanks(cp);	/* Ignore leading white space characters */
  switch (*cp) {
  case '&':	/* Hex value */
    cp++;
    while (isxdigit(*cp)) {
      digits++;
      value = (value<<4)+todigit(*cp);
      value64 = (value64<<4)+todigit(*cp);
      cp++;
    }
    if (digits==0) {
      *intvalue = WARN_BADHEX;	/* Bad hexadecimal constant */
      *int64value = WARN_BADHEX;	/* Bad hexadecimal constant */
      cp = NIL;
    }
    else {
      *intvalue = value;
      if (!matrixflags.hex64)
        *int64value = (int64)value;
      else
        *int64value = value64;
      *isinteger = TRUE;
    }
    break;
  case '%':	/* Binary value */
    cp++;
    while (*cp=='0' || *cp=='1') {
      digits++;
      value = (value<<1)+(*cp-'0');
      value64 = (value64<<1)+(*cp-'0');
      cp++;
    }
    if (digits==0) {
      *intvalue = WARN_BADBIN;	/* Bad binary constant */
      *int64value = WARN_BADBIN;	/* Bad binary constant */
      cp = NIL;
    }
    else {
      *intvalue = value;
      if (!matrixflags.hex64)
        *int64value = (int64)value;
      else
        *int64value = value64;
      *isinteger = TRUE;
    }
    break;
  default:	/* Integer or floating point value */
    isint = TRUE;
    isneg = *cp=='-';	/* Deal with any sign first */
    if (*cp=='+' || *cp=='-') cp++;
    while (*cp>='0' && *cp<='9') {
      digits = 0;	/* Used to count the number of digits before the '.' */
      if (isint && value64>=INT64CONV) {
        isint = FALSE;
        fpvalue = TOFLOAT(value64);
      }
      if (isint) {
        value = value*10+(*cp-'0');
        value64 = value64*10ll+(*cp-'0');
      } else {
        fpvalue = fpvalue*10.0+TOFLOAT(*cp-'0');
      }
      digits++;
      cp++;
    }
    if (!isint && *cp!='.' && *cp!='E' && fpvalue<=TOFLOAT(MAXINTVAL)) {	/* Convert back to integer */
      value = TOINT(fpvalue);
      value64 = TOINT64(fpvalue);
      isint = TRUE;
    }
    if (*cp=='.') {	/* Number contains a decimal point */
      float64 fltdiv;
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
      boolean negexp;
      int exponent;
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
    if (isint) {
      *intvalue = (isneg ? -value : value);
      *int64value = (isneg ? -value64 : value64);
    } else {
      *floatvalue = (isneg ? -fpvalue : fpvalue);
    }
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function convert.c:tonumber\n");
#endif
  return cp;
}

/*
** 'todecimal' converts the character string starting at 'cp' to binary.
** It handles integer and floating point values in decimal only.
** It returns a pointer to the character after the last one used in the
** number or 'NIL' if an error was detected.  The value is returned at
** either 'floatvalue' or 'intvalue' depending on the type of the number.
** 'isinteger' says which it is. In the event of an error, 'intvalue' is
** used to return an error number
*/
char *todecimal(char *cp, boolean *isinteger, int32 *intvalue, int64 *int64value, float64 *floatvalue) {
  int32 value;
  int64 value64;
  float64 fpvalue;
  boolean isint, isneg;
  int digits;

#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, ">>> Entered function convert.c:tonumber\n");
#endif
  value = 0;
  value64 = 0;
  fpvalue = 0;
  digits = 0;
  cp = skip_blanks(cp);	/* Ignore leading white space characters */
  isint = TRUE;
  isneg = *cp=='-';	/* Deal with any sign first */
  if (*cp=='+' || *cp=='-') cp++;
  while (*cp>='0' && *cp<='9') {
    digits = 0;	/* Used to count the number of digits before the '.' */
    if (isint && value64>=INT64CONV) {
      isint = FALSE;
      fpvalue = TOFLOAT(value64);
    }
    if (isint) {
      value = value*10+(*cp-'0');
      value64 = value64*10ll+(*cp-'0');
    } else {
      fpvalue = fpvalue*10.0+TOFLOAT(*cp-'0');
    }
    digits++;
    cp++;
  }
  if (!isint && *cp!='.' && *cp!='E' && fpvalue<=TOFLOAT(MAXINTVAL)) {	/* Convert back to integer */
    value = TOINT(fpvalue);
    value64 = TOINT64(fpvalue);
    isint = TRUE;
  }
  if (*cp=='.') {	/* Number contains a decimal point */
    float64 fltdiv;
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
    int exponent;
    boolean negexp;
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
  if (isint) {
    *intvalue = (isneg ? -value : value);
    *int64value = (isneg ? -value64 : value64);
  } else {
    *floatvalue = (isneg ? -fpvalue : fpvalue);
  }
#ifdef DEBUG
  if (basicvars.debug_flags.functions) fprintf(stderr, "<<< Exited function convert.c:tonumber\n");
#endif
  return cp;
}
