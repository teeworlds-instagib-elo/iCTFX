#include "connection.h"

#include <engine/shared/protocol.h>

void IDbConnection::FormatCreateUsers(char *aBuf, unsigned int BufferSize)
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS `stats` ("
			"`name` varchar(255) PRIMARY KEY,"
			"`kills` int,"
			"`deaths` int,"
			"`touches` int,"
			"`captures` int,"
			"`fastest_capture` int,"
			"`suicides` int,"
			"`shots` int,"
			"`wallshots` int,"
			"`wallshot_kills` int"
		");"
		);
}

void IDbConnection::FormatCreateServer(char *aBuf, unsigned int BufferSize)
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS `stats_server` ("
			"`server_name` varchar(255) PRIMARY KEY,"
			"`red_score` int,"
			"`blue_score` int"
		");"
		);
}