/*
 * xpra.h - sharing this server's windows, one by one, with Xpra clients.
 * See xpra.c.
 */

/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: 2026 D. Kevin Cameron
 *
 * This is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  It is distributed WITHOUT ANY WARRANTY; see
 * the file LICENCE.TXT.
 */

#ifndef XPRA_H
#define XPRA_H

/* TRUE once -xpra, -xprasocket or -xpraport was given */
extern Bool xpraEnabled;

/* command line: returns the number of arguments used, 0 if not ours */
extern int xpraProcessArgument(int argc, char *argv[], int i);
extern void xpraUseMsg(void);

/* called from InitOutput, at start and again after every server reset */
extern void xpraInit(void);
/* called when the screen has been set up, after xpraInit */
extern void xpraScreenInit(ScreenPtr pScreen);
/* called from ProcessInputEvents */
extern void xpraCheckFds(void);
/* called when the server exits */
extern void xpraShutdown(void);

/* a part of the screen is about to change (screen coordinates) */
extern void xpraAddDamage(ScreenPtr pScreen, RegionPtr pRegion);

#endif /* XPRA_H */
