#ifndef SQL_HANDLER_H
#define SQL_HANDLER_H

#include <atomic>
#include <iostream>
#include <memory>
#include <stdint.h>
#include <string>
#include <thread>

#include <cxxpool.h>
#include <eventpp/eventqueue.h>

#include <engine/server/databases/connection.h>
#include <game/server/player.h>

#include <engine/shared/config.h>

class SqlHandler
{
	enum class EventType
	{
		CreatePlayer,
		SetStats,
		SetServerStats,
		ShowTop5,
		ShowRank
	};

	struct GetPlayerStatsData
	{
		CPlayer *pPlayer;
		std::string player_name;
	};

	struct SetStatsData
	{
		std::string m_player_name;
		Stats m_stats;
	};

	struct ShowTop5Data {
		CPlayer *pPlayer;
	};

	struct SetServerStatsData
	{
		ServerStats stats;
	};

public:
	SqlHandler();

	/// starts all threads
	void start();

	/// stops all threads. ATTENTION: this method blocks until all threads have stopped.
	void stop();

	/// dispatches the passed data to the corresponding queue
	void get_player_stats(CPlayer *pPlayer, const std::string player_name);

	/// dispatches the passed data to the corresponding queue
	void set_stats(const std::string player_name, const Stats stats);

	/// dispatches the passed data to the corresponding queue
	void set_server_stats(const ServerStats stats);

	void show_top5(CPlayer *pPlayer);

	void show_rank(CPlayer *pPlayer, const std::string player_name);

private:
	void threadloop();

	static void get_player_stats_handler(void *ev);
	static void set_stats_handler(void *ev);
	static void set_server_stats_handler(void *ev);
	static void show_top5_handler(void *ev);
	static void show_rank_handler(void *ev);

	std::function<void(void *)> make_callback(std::function<void(void *)> callback)
	{
		return [this, callback](void *ev) {
			m_thread_pool.push(
				[callback](void *data) {
					callback(data);
				},
				ev);
		};
	}

	std::atomic<bool> m_should_stop;
	std::thread m_thread;
	eventpp::EventQueue<EventType, void(void *)> m_queue;
	cxxpool::thread_pool m_thread_pool;
};
#endif