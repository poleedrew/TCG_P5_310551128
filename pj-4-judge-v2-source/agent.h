/**
 * Framework for NoGo and similar games (C++ 11)
 * agent.h: Define the behavior of variants of the player
 *
 * Author: Theory of Computer Games (TCG 2021)
 *         Computer Games and Intelligence (CGI) Lab, NYCU, Taiwan
 *         https://cgilab.nctu.edu.tw/
 */

#pragma once
#include <string>
#include <random>
#include <sstream>
#include <map>
#include <type_traits>
#include <algorithm>
#include "board.h"
#include "action.h"
#include <fstream>
#include <chrono>
#include <cassert>

class agent {
public:
	agent(const std::string& args = "") {
		std::stringstream ss("name=unknown role=unknown " + args);
		for (std::string pair; ss >> pair; ) {
			std::string key = pair.substr(0, pair.find('='));
			std::string value = pair.substr(pair.find('=') + 1);
			meta[key] = { value };
		}
	}
	virtual ~agent() {}
	virtual void open_episode(const std::string& flag = "") {}
	virtual void close_episode(const std::string& flag = "") {}
	virtual action take_action(const board& b) { return action(); }
	virtual bool check_for_win(const board& b) { return false; }

public:
	virtual std::string property(const std::string& key) const { return meta.at(key); }
	virtual void notify(const std::string& msg) { meta[msg.substr(0, msg.find('='))] = { msg.substr(msg.find('=') + 1) }; }
	virtual std::string name() const { return property("name"); }
	virtual std::string role() const { return property("role"); }

protected:
	typedef std::string key;
	struct value {
		std::string value;
		operator std::string() const { return value; }
		template<typename numeric, typename = typename std::enable_if<std::is_arithmetic<numeric>::value, numeric>::type>
		operator numeric() const { return numeric(std::stod(value)); }
	};
	std::map<key, value> meta;
};

/**
 * base agent for agents with randomness
 */
class random_agent : public agent {
public:
	random_agent(const std::string& args = "") : agent(args) {
		if (meta.find("seed") != meta.end())
			engine.seed(int(meta["seed"]));
	}
	virtual ~random_agent() {}

protected:
	std::default_random_engine engine;
};

/**
 * player for both side
 */
class player : public random_agent {
public:
	player(const std::string& args = "") : random_agent("N=0 T=0 c=0.1 psi=-1 " + args),
		mcts(size_t(meta["N"]) | size_t(meta["T"])),
		space(board::size_x * board::size_y), who(board::empty) {
		if (meta.find("weak") != meta.end()) { // TCG weak sample player
			mcts = true;
			meta["name"] = {"weak"};
			meta["N"] = {"200"};
			meta["c"] = {"0.1"};
			meta["psi"] = {"1"};
		} else if (meta.find("medium") != meta.end()) { // TCG medium sample player
			mcts = true;
			meta["name"] = {"medium"};
			meta["N"] = {"10000"};
			meta["c"] = {"0.1"};
			meta["psi"] = {"1"};
		} else if (meta.find("strong") != meta.end()) { // TCG strong sample player
			mcts = true;
			meta["name"] = {"strong"};
			meta["N"] = {"10000"};
			meta["c"] = {"0.2"};
			meta["psi"] = {"-1"};
		} else if (meta.find("random") != meta.end()) { // TCG random sample player
			mcts = false;
			meta["name"] = {"random"};
		}
#if defined(JUDGE)
		else { // in judge mode, "unlock!" is required for configuring MCTS
			mcts &= (meta.find("unlock!") != meta.end());
		}
#endif
		if (name().find_first_of("[]():; ") != std::string::npos)
			throw std::invalid_argument("invalid name: " + name());
		if (role() == "black") who = board::black;
		if (role() == "white") who = board::white;
		if (who == board::empty)
			throw std::invalid_argument("invalid role: " + role());
		for (size_t i = 0; i < space.size(); i++)
			space[i] = action::place(i, who);
	}

	class node {
	public:
		node(const board& s, const std::vector<board::point>& pempty, std::default_random_engine& engine, const board::point& p)
		: state(s), move(p, 3 - s.info().who_take_turns), win(board::unknown), value(0), visit(0) {
			legal.reserve(pempty.size());
			empty.reserve(pempty.size());
			for (const board::point& p : pempty) {
				if (state[p.x][p.y] != board::empty) continue;
				empty.push_back(p);
				if (board(state).place(p) == board::legal)
					legal.push_back(p);
			}
			std::shuffle(legal.begin(), legal.end(), engine);
			child.reserve(legal.size());
			if (legal.empty()) win = move.color();
		}
		node(const board& s, std::default_random_engine& engine) : node(s, ({
			std::vector<board::point> empty;
			for (unsigned i = 0; i < 81; i++)
				if (s(i) == board::empty) empty.push_back(i);
			empty;
		}), engine, {}) {}
		~node() {
			for (auto ndptr : child) delete ndptr;
			child.clear();
		}
	public:
		void run_mcts(float c, float psi, std::default_random_engine& engine) {
			std::vector<node*> nodes;
			float ps = 1;
			nodes.push_back(this);
			while (nodes.back()->is_fully_expanded()) {
				nodes.push_back(nodes.back()->select(c, ps));
				ps *= psi;
			}
			node* leaf = nodes.back()->expand(engine);
			board::piece_type who = nodes.back()->win;
			if (leaf) {
				who = leaf->rollout(engine);
				nodes.push_back(leaf);
			}
			int z = (who == state.info().who_take_turns);
			while (nodes.size()) {
				nodes.back()->update(z);
				nodes.pop_back();
			}
		}
		bool is_fully_expanded() const {
			return child.size() && legal.empty();
		}
		void update(int z) {
			value += z;
			visit += 1;
		}
		node* select(float c, float ps) const {
			float log_visit = std::log(visit);
			node* best_node = nullptr;
			float best_ucb = -std::numeric_limits<float>::max();
			for (auto ndptr : child) {
				float Q = float(ndptr->value) / ndptr->visit;
				float n = ndptr->visit;
				float ucb = ps * Q + c * std::sqrt(log_visit / n);
				if (ucb > best_ucb) {
					best_node = ndptr;
					best_ucb = ucb;
				}
			}

			return best_node;
		}
		node* expand(std::default_random_engine& engine) {
			if (legal.size()) {
				board test = state;
				int r = test.place(legal.back());
				assert(r == board::legal);
				child.push_back(new node(test, empty, engine, legal.back()));
				legal.pop_back();
				return child.back();
			} else {
				return nullptr;
			}
		}
		board::piece_type rollout(std::default_random_engine& engine) {
			std::shuffle(empty.begin(), empty.end(), engine);
			board rollout = state;
			for (size_t i = 0, n = empty.size(); n;) {
				if (rollout.place(empty[n - 1]) == board::legal) {
					n -= 1;
					i = 0;
				} else if (i < n) {
					std::swap(empty[i++], empty[n - 1]);
				} else {
					n = 0;
				}
			}
			for (size_t i = 0; i < empty.size(); i++) {
				assert(rollout.place(empty[i]) != board::legal);
			}
			return static_cast<board::piece_type>(3 - rollout.info().who_take_turns);
		}
		action best() const {
			if (child.empty()) return action();
			node* best = child[0];
			for (auto ndptr : child)
				if (ndptr->visit > best->visit)
					best = ndptr;
			return best->move;
		}
	protected:
		board state;
		action::place move;
		std::vector<node*> child;
		std::vector<board::point> legal;
		std::vector<board::point> empty;
		board::piece_type win;
		size_t value;
		size_t visit;
	};

	virtual action take_action(const board& state) {
		if (mcts) {
			float c = meta["c"];
			float psi = meta["psi"];
			time_t T = meta["T"];
			node root(state, engine);
			if (T) {
				time_t limit = millisec() + T - 5;
				while (millisec() < limit) {
					for (size_t i = 0; i < 10; i++) root.run_mcts(c, psi, engine);
				}
			} else {
				size_t N = meta["N"] ?: 1000;
				while (N--) root.run_mcts(c, psi, engine);
			}
			return root.best();
		}
		std::shuffle(space.begin(), space.end(), engine);
		for (const action::place& move : space) {
			board after = state;
			if (move.apply(after) == board::legal)
				return move;
		}
		return action();
	}

protected:
	static time_t millisec() {
		auto now = std::chrono::system_clock::now().time_since_epoch();
		return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
	}

private:
	bool mcts;
	std::vector<action::place> space;
	board::piece_type who;
};
