/*
** Copyright (C) 2009-2020 Quadrant Information Security <quadrantsec.com>
** Copyright (C) 2009-2020 Champ Clark III <cclark@quadrantsec.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

int  Flexbit_Type ( char *, int, const char *);

bool Flexbit_Count( int rule_position, char *ip_src_char, char *ip_dst_char );
bool Flexbit_Condition(int rule_position, char *ip_src_char, char *ip_dst_char, int src_port, int dst_port, char *username );
void Flexbit_Set(int rule_position, char *ip_src_char, char *ip_dst_char, int src_port, int dst_port, char *username, _Sagan_Proc_Syslog *SaganProcSyslog_LOCAL );


