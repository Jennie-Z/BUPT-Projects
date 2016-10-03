#ifndef POKEMON_SERVER_H
#define POKEMON_SERVER_H

#include <vector>
#include <string>
#include <unordered_map>
#include <iostream>
#include <mutex>
#include <memory>

#include "Pokemon.h"
#include "Socket.h"
#include "Model.h"

namespace PokemonGame_Impl
{
	std::vector<std::string> SplitStr (const std::string &input,
									   const std::string &delimiter)
	{
		std::vector<std::string> ret;
		size_t pos = 0;
		std::string inputStr = input;
		while ((pos = inputStr.find (delimiter)) != std::string::npos)
		{
			ret.push_back (inputStr.substr (0, pos));
			inputStr.erase (0, pos + delimiter.length ());
		}
		ret.push_back (inputStr);
		return std::move (ret);
	}
}

namespace PokemonGame
{
	class PokemonServer
	{
	public:
		PokemonServer (unsigned short port)
		{
			using namespace PokemonGame_Impl;
			using namespace BOT_ORM;
			const auto dbName = "Pokemon.db";
			ORMapper<PokemonModel> pokemonMapper (dbName);
			ORMapper<UserModel> userMapper (dbName);
			ORMapper<SessionModel> sessionMapper (dbName);

			// Init DB
			if (pokemonMapper.CreateTbl ())
				std::cout << "Created Pokemon\n";
			else
				std::cerr << pokemonMapper.ErrMsg () << std::endl;

			if (userMapper.CreateTbl ())
				std::cout << "Created Pokemon\n";
			else
				std::cerr << userMapper.ErrMsg () << std::endl;

			if (sessionMapper.CreateTbl ())
				std::cout << "Created Pokemon\n";
			else
				std::cerr << sessionMapper.ErrMsg () << std::endl;

			// Check Args and Login
			auto CheckArgAndLogin = [&] (std::string &response,
										 const std::vector<std::string> &args,
										 size_t argCount,
										 bool isLoginRequired)
			{
				if (args.size () < argCount)
				{
					SetResponse (response, false, "Too Few Arguments");
					return false;
				}

				if (isLoginRequired)
				{
					if (args.size () < 2)
					{
						SetResponse (response, false, "Server Error");
						return false;
					}

					const auto &sid = args[1];
					if (!sessionMapper.Count ("where sid='" + sid + "'"))
					{
						SetResponse (response, false, "You haven't Login");
						return false;
					}
				}

				return true;
			};

			// Allocate Pokemon to User
			// Return true if Insert New Pokemon Successfully
			auto AddPokemon = [&] (const std::string &uid,
								   const Pokemon &pokemon)
			{
				auto newPkm = std::unique_ptr<PokemonModel> (
					PokemonModel::NewFromPokemon (pokemon));

				newPkm->id = pokemonMapper.Count ();
				if (!pokemonMapper.Insert (*newPkm))
					return false;

				return true;
			};

			// Handle Login
			// Succeed if Set Sid / Get Sid is OK
			// Fail if No Uid Found / Wrong Pwd / Get Sid Failed
			SetHandler ("Login", [&] (std::string &response,
									  bool &isKeepAlive,
									  const std::vector<std::string> &args)
			{
				if (!CheckArgAndLogin (response, args, 3, false))
					return;

				const auto &uid = args[1];
				const auto &pwd = args[2];

				std::vector<UserModel> users;
				userMapper.Select (users, "where uid='" + uid + "'");

				// Wrong PWD
				if (users.empty () || users[0].pwd != pwd)
				{
					SetResponse (response, false, "Bad Login Attempt");
					return;
				}

				// New Sid
				auto sid = uid + std::to_string (time (nullptr));
				sid = sid.substr (0, 32);
				SessionModel session { uid, sid };
				if (sessionMapper.Insert (session))
				{
					SetResponse (response, true, sid);
					return;
				}

				// Get Sid From Uid
				std::vector<SessionModel> sessions;
				sessionMapper.Select (sessions, "where uid='" + uid + "'");
				if (!sessions.empty ())
				{
					SetResponse (response, true, sessions[0].sid);
					return;
				}

				// No such Logic :-(
				SetResponse (response, false, "Server Error");
			});

			// Handle Register
			// Succeed if Set Uid and New Pokemon are OK
			// Fail if Set Uid / New Pokemon Failed
			SetHandler ("Register", [&] (std::string &response,
										 bool &isKeepAlive,
										 const std::vector<std::string> &args)
			{
				if (!CheckArgAndLogin (response, args, 3, false))
					return;
				const auto &uid = args[1];
				const auto &pwd = args[2];

				UserModel user { uid, pwd, 1, 0,
					"New Commer\n"
					"Welcome to Pokemon Game\n"
					"Hello World" };
				if (!userMapper.Insert (user))
				{
					SetResponse (response, false, "UserID has been Taken");
					return;
				}

				// Seed 3 Init Pokemon
				const auto initPokemonCount = 3;
				auto isAddPokemon = true;
				for (size_t i = 0; i < initPokemonCount; i++)
				{
					auto newPokemon =
						std::unique_ptr<Pokemon> (NewPokemonRandly ());
					if (!AddPokemon (uid, *newPokemon))
						isAddPokemon = false;
				}

				if (isAddPokemon)
					SetResponse (response, true,
								 "Registered successfully");
				else
					SetResponse (response, false,
								 "Registered successfully but "
								 "Allocate Pokemon Failed :-(");
			});

			// Handle Logout
			// Succeed if Delete Sid is OK
			// Fail if Delete Sid Failed
			SetHandler ("Logout", [&] (std::string &response,
									   bool &isKeepAlive,
									   const std::vector<std::string> &args)
			{
				if (!CheckArgAndLogin (response, args, 2, true))
					return;
				const auto &sid = args[1];

				if (sessionMapper.Delete ("where sid='" + sid + "'"))
					SetResponse (response, true, "Logout Successfully");
				else
					SetResponse (response, false, "Logout Failed");
			});

			// Handle UsersPokemons
			// Succeed if Pokemon Found
			// Fail if No Sid Found / No Pokemon Found / Select Failed
			SetHandler ("UsersPokemons", [&] (std::string &response,
											  bool &isKeepAlive,
											  const std::vector<std::string> &args)
			{
				if (!CheckArgAndLogin (response, args, 3, true))
					return;
				const auto &uidToSearch = args[2];

				std::vector<PokemonModel> pokemons;
				pokemonMapper.Select (pokemons,
									 "where uid='" + uidToSearch + "'");
				if (pokemons.empty ())
				{
					SetResponse (response, false, "No Pokemon Found");
					return;
				}

				std::string ret;
				for (auto & pokemon : pokemons)
				{
					ret += std::move (pokemon.name);
					ret += '\n';
				}
				ret.pop_back ();
				SetResponse (response, true, ret);
			});

			// Handle UsersWonRate
			// Succeed if User Found
			// Fail if No Sid Found / No User Found / Select Failed
			SetHandler ("UsersWonRate", [&] (std::string &response,
											 bool &isKeepAlive,
											 const std::vector<std::string> &args)
			{
				if (!CheckArgAndLogin (response, args, 3, true))
					return;
				const auto &uidToSearch = args[2];

				std::vector<UserModel> users;
				userMapper.Select (users,
								  "where uid='" + uidToSearch + "'");

				if (users.empty ())
					SetResponse (response, false, "No Such User");
				else
					SetResponse (response, true,
								 std::to_string (
								 (double) users[0].won
									 / (users[0].won + users[0].los)));
			});

			// Handle UsersBadges
			// Succeed if Badges Found
			// Fail if No Sid Found / No User Found / Select Failed
			SetHandler ("UsersBadges", [&] (std::string &response,
											bool &isKeepAlive,
											const std::vector<std::string> &args)
			{
				if (!CheckArgAndLogin (response, args, 3, true))
					return;
				const auto &uidToSearch = args[2];

				std::vector<UserModel> users;
				userMapper.Select (users,
								  "where uid='" + uidToSearch + "'");

				if (users.empty ())
					SetResponse (response, false, "No Badge Found");
				else
					SetResponse (response, true, users[0].badge);
			});

			// Handle UsersAll
			// Succeed if User Found
			// Fail if No Sid Found / No User Found / Select Failed
			SetHandler ("UsersAll", [&] (std::string &response,
										 bool &isKeepAlive,
										 const std::vector<std::string> &args)
			{
				if (!CheckArgAndLogin (response, args, 2, true))
					return;

				std::vector<UserModel> users;
				userMapper.Select (users);

				if (users.empty ())
				{
					SetResponse (response, false, "No User Registered");
					return;
				}

				std::string ret;
				for (auto & user : users)
				{
					ret += std::move (user.uid);
					ret += '\n';
				}
				ret.pop_back ();
				SetResponse (response, true, ret);
			});

			// Handle UsersOnline
			// Succeed if User Found
			// Fail if No Sid Found / No User Found / Select Failed
			SetHandler ("UsersOnline", [&] (std::string &response,
											bool &isKeepAlive,
											const std::vector<std::string> &args)
			{
				if (!CheckArgAndLogin (response, args, 2, true))
					return;

				std::vector<SessionModel> sessions;
				sessionMapper.Select (sessions);

				if (sessions.empty ())
				{
					SetResponse (response, false, "No User Online");
					return;
				}

				std::string ret;
				for (auto & session : sessions)
				{
					ret += std::move (session.uid);
					ret += '\n';
				}
				ret.pop_back ();
				SetResponse (response, true, ret);
			});

			// Quit
			SetHandler ("Quit", [&] (std::string &response,
									 bool &isKeepAlive,
									 const std::vector<std::string> &args)
			{
				isKeepAlive = false;
				SetResponse (response, true, "End of Session");
			});

			// Run
			std::mutex ioLock;
			BOT_Socket::Server (port, [&] (const std::string &request,
										   std::string &response,
										   bool &isKeepAlive)
			{
				// Keep Alive by Default
				isKeepAlive = true;

				auto args = SplitStr (request, "\n");
				if (args.size () < 1)
					SetResponse (response, false, "No Arguments");
				else if (_handlers.find (args[0]) == _handlers.end ())
					SetResponse (response, false, "No Handler Found");
				else
					_handlers[args[0]] (response, isKeepAlive, args);

				std::lock_guard<std::mutex> lck (ioLock);
				std::cout << '\n' << request << '\n' << response << "\n\n";
			});
		}

	private:
		static void SetResponse (std::string &response,
								 bool isSucceeded,
								 const std::string &param)
		{
			response = (isSucceeded ? "1\n" : "0\n") + param;
		}

		void SetHandler (std::string cmd,
						 std::function<void (std::string &response,
											 bool &isKeepAlive,
											 const std::vector<std::string>
											 &fullCmd)> handler)
		{
			_handlers.emplace (std::move (cmd), std::move (handler));
		}

		std::unordered_map<std::string,
			std::function<void (std::string &, bool &,
								const std::vector<std::string> &)>>
			_handlers;
	};

}

#endif // !POKEMON_SERVER_H