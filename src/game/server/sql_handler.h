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

class SqlHandler
{
	enum class EventType
	{
		CreatePlayer,
		SetStats,
		SetServerStats,
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
	void get_player_stats(CPlayer *pPlayer, const std::string &player_name);

	/// dispatches the passed data to the corresponding queue
	void set_stats(const std::string &player_name, const Stats &stats);

	/// dispatches the passed data to the corresponding queue
	void set_server_stats(const ServerStats &stats);

private:
	void threadloop();

	template<typename T>
	std::function<void(void *)> make_callback(std::function<void(T *, IDbConnection *)> &&callback)
	{
		auto database = CreateMysqlConnection("ddnet", "record", "ddnet", "thebestpassword", "localhost", 3306, true);
		return [this, callback, &database](void *ev) {
			auto data = static_cast<T *>(ev);
			m_thread_pool.push(
				[this, &database](std::function<void(T *, IDbConnection *)> callback, T *data) {
					callback(data, database);
				},
				callback, data);
		};
	}

	std::atomic<bool> m_should_stop;
	std::thread m_thread;
	eventpp::EventQueue<EventType, void(void *)> m_queue;
	cxxpool::thread_pool m_thread_pool;
};