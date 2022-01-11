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
#include <functional>
#include <time.h>
#include <thread>
#include <unordered_map>
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
 * MCTS: perform N cycles and take the best action by visit count
 * random: put a legal piece randomly
 */
class player : public random_agent {
public:
	player(const std::string& args = "") : random_agent("name=random role=unknown N=0 T=0 thread=0 C=0.3" + args),
		space(board::size_x * board::size_y), who(board::empty) {
		if (name().find_first_of("[]():; ") != std::string::npos)
			throw std::invalid_argument("invalid name: " + name());
		if (role() == "black") who = board::black;
		if (role() == "white") who = board::white;
		if (who == board::empty)
			throw std::invalid_argument("invalid role: " + role());
		for (size_t i = 0; i < space.size(); i++)
			space[i] = action::place(i, who);
	}

	class node : board {
	public:
		node(const board& state, node* parent = nullptr, int position = -1) : board(state),
			win(0), visit(0), child(), pos_(position), parent(parent) {}
		
		/**
		 * run MCTS for N cycles and retrieve the best action
		 */
		action run_mcts(size_t N, std::default_random_engine& engine, double exploration) {
			
			for (size_t i = 0; i < N; i++) {
				std::vector<node*> path = select(exploration);
				node* leaf = path.back()->expand(engine);
				if (leaf != path.back()) path.push_back(leaf);
				update(path, leaf->simulate(engine));
			}
			return take_action();
		}
		/**
		 * run MCTS for T milliseconds and retrieve the best action
		 */
		action run_mcts_t(size_t T, std::default_random_engine& engine, double exploration) {
			double start, end;
			start = clock();
			end = clock();
			while(end - start + 10 < T) {
				std::vector<node*> path = select(exploration);
				node* leaf = path.back()->expand(engine);
				if (leaf != path.back()) path.push_back(leaf);
				update(path, leaf->simulate(engine));
				end = clock();
			}
			return take_action();
		}
	protected:

		/**
		 * select from the current node to a leaf node by UCB and return all of them
		 * a leaf node can be either a node that is not fully expanded or a terminal node
		 */
		std::vector<node*> select(double exploration) {
			std::vector<node*> path = { this };
			for (node* ndptr = this; ndptr->is_selectable(); path.push_back(ndptr)) {
				ndptr = &*std::max_element(ndptr->child.begin(), ndptr->child.end(),
						[=](const node& lhs, const node& rhs) { return lhs.ucb_score(exploration) < rhs.ucb_score(exploration); });
			}
			return path;
		}

		/**
		 * expand the current node and return the newly expanded child node
		 * if the current node has no unexpanded move, it returns itself
		 */
		node* expand(std::default_random_engine& engine) {
			board child_state = *this;
			std::vector<int> moves = all_moves(engine);
			auto expanded_move = std::find_if(moves.begin(), moves.end(), [&](int move) {
				// check whether it is an unexpanded legal move
				bool is_expanded = std::find_if(child.begin(), child.end(),
						[&](const node& node) { return node.info().last_move.i == move; }) != child.end();
				return is_expanded == false && child_state.place(move) == board::legal;
			});
			if (expanded_move == moves.end())return this; // already terminal
			child.emplace_back(child_state, this, *expanded_move);
			return &child.back();
		}

		/**
		 * simulate the current node and return the winner
		 */
		unsigned simulate(std::default_random_engine& engine) {
			board rollout = *this;
			std::vector<int> moves = all_moves(engine);
			while (std::find_if(moves.begin(), moves.end(),
					[&](int move) { return rollout.place(move) == board::legal; }) != moves.end());
			return (rollout.info().who_take_turns == board::white) ? board::black : board::white;
		}

		/**
		 * update statistics for all nodes saved in the path
		 */
		void update(std::vector<node*>& path, unsigned winner) {
			for (node* ndptr : path) {
				ndptr->win += (winner == info().who_take_turns) ? 1 : 0;
				ndptr->visit += 1;
			}
		}

		/**
		 * pick the best action by visit counts
		 */
		action take_action() const {
			auto best = std::max_element(child.begin(), child.end(),
					[](const node& lhs, const node& rhs) { return lhs.visit < rhs.visit; });
			if (best == child.end()) return action(); // no legal move
			return action::place(best->info().last_move, info().who_take_turns);
		}

	private:

		/**
		 * check whether this node is a fully-expanded non-terminal node
		 */
		bool is_selectable() const {
			size_t num_moves = 0;
			for (int move = 0; move < 81; move++)
				if (board(*this).place(move) == board::legal)
					num_moves++;
			return child.size() == num_moves && num_moves > 0;
		}

		/**
		 * get the ucb score of this node
		 */
		float ucb_score(float c = std::sqrt(2)) const {
			float exploit = float(win) / visit;
			float explore = std::sqrt(std::log(parent->visit) / visit);
			return exploit + c * explore;
		}

		/**
		 * get all moves in shuffled order
		 */
		std::vector<int> all_moves(std::default_random_engine& engine) const {
			std::vector<int> moves;
			for (int move = 0; move < 81; move++) moves.push_back(move);
			std::shuffle(moves.begin(), moves.end(), engine);
			return moves;
		}
		
	public:	
		size_t win, visit;
		int pos_;
		std::vector<node> child;
		node* parent;
	};

	virtual action take_action(const board& state) {
		size_t N = meta["N"];
		size_t T = meta["T"];
		double C = meta["C"];
		size_t thread_num = meta["thread"];
		// auto opponent_type = who == board::black ? board::white: board::black;
		// int opponent_number = 0;
		// for(size_t i=0; i < 9;i++)
		// 	for (size_t j=0;j<9;j++)
		// 		if(state[i][j] == opponent_type) opponent_number++;
		// if(space.size() == 0){
		// 	return action();
		// }
		// if (opponent_number < 25)T = T * 4;
		// else if(opponent_number < 30)T = T * 1;
		// else T = T * 0.6;
		
		
		if (N){
			if(thread_num){
				
				std::vector<std::thread> t;
				std::vector<node> roots(thread_num, state);
				
				for(size_t i=0; i < thread_num; i++)
					t.push_back(std::thread(&node::run_mcts, &roots[i], N, std::ref(engine), C));
				
				for(size_t j=0; j < thread_num; j++)
					t[j].join();
				std::unordered_map<int, std::pair<int,int>> cal;
				for(size_t i=0; i < thread_num; i++){
					for(size_t j=0; j < roots[i].child.size(); j++){
						int index = roots[i].child[j].pos_;
						cal[index].first += roots[i].child[j].win;
						cal[index].second += roots[i].child[j].visit;
					}
				}
				auto best = cal.begin();
				for(auto i = cal.begin(); i != cal.end(); i++){
					float mean = i->second.first / i->second.second;
					float b_mean = best->second.first / best->second.second;
					if(mean > b_mean){
						best = i;
					}
				}
				if(best->first != -1) return action::place(best->first, this->who);
			}
			else
				return node(state).run_mcts(N, engine, C);
		} 
		if (T){
			if(thread_num){
				
				std::vector<std::thread> t;
				std::vector<node> roots(thread_num, state);
				
				for(size_t i=0; i < thread_num; i++)
					t.push_back(std::thread(&node::run_mcts_t, &roots[i], T, std::ref(engine), C));
				
				for(size_t j=0; j < thread_num; j++)
					t[j].join();
				std::unordered_map<int, std::pair<int,int>> cal;
				for(size_t i=0; i < thread_num; i++){
					for(size_t j=0; j < roots[i].child.size(); j++){
						int index = roots[i].child[j].pos_;
						if(cal.find(index) == cal.end()) {
							cal[index].first = roots[i].child[j].win;
							cal[index].second = roots[i].child[j].visit;
						}else
						{
							cal[index].first += roots[i].child[j].win;
							cal[index].second += roots[i].child[j].visit;
						}
					}
				}
				auto best = cal.begin();
				for(auto i = cal.begin(); i != cal.end(); i++){
					if(best->second.second < i->second.second ) best = i;
					else if(best->second.second == i->second.second){
						best = best->second.first > i->second.first ? best : i;
					}
					// float mean = i->second.second ? i->second.first / i->second.second : i->second.first;
					// float b_mean = best->second.second ? best->second.first / best->second.second : best->first;
					// if(mean > b_mean){
					// 	best = i;
					// }
				}
				if(best->first > -1 && best->first < 81 ) return action::place(best->first, this->who);
			}
			else
				return node(state).run_mcts_t(T, engine, C);
		} 
		std::shuffle(space.begin(), space.end(), engine);
		for (const action::place& move : space) {
			board after = state;
			if (move.apply(after) == board::legal)
				return move;
		}
		return action();
	}

private:
	std::vector<action::place> space;
	board::piece_type who;
};
