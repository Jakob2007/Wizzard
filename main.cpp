#include <iostream>
#include <cassert>
#include <random>
#include <algorithm>
using namespace std;

/*

cd /Users/jakobsauer/Documents/Development/C++/Wizzard
clang++ main.cpp -o wizzard; ./wizzard

*/

/* ---- RUNS -----
- davor ca 80ms pro 1.000

- no hand copy update:
average time per run (ms):
59
average time per 1.000 calls (ms):
48
average calls per run:
1213
- with -O3
average time per run (ms):
6
average time per 1.000 calls (ms):
4
average calls per run:
1304


- previous find_valid_cards was faulty, updated values:
average time per run (ms):
27
average time per 1.000 calls (ms):
26
average calls per run:
1062

- target hitting; why so bad??
average time per run (ms):
89
average time per 1.000 calls (ms):
40
average calls per run:
2205

*/


void info(const string &text="", bool no_end=false) { cout << "\x1b[36m" << text << "\x1b[0m" << (no_end ? "" : "\n"); }
void debug(const string &text) { cout << text << std::endl; }
static thread_local mt19937_64 random_seed{ std::random_device{}() };

namespace Card {
	using card_t = uint8_t;
	constexpr card_t EMPTY = 0b00000000;
	enum Color : card_t {
		RED    = 0b00000000,
		GREEN  = 0b00100000,
		BLUE   = 0b01000000,
		YELLOW = 0b01100000,
		MAGIC  = 0b10000000,
	};
	const char* color_names[5] = {"Red","Green","Blue","Yellow","Magic"};
	const char* color_display_prefix[5] = {"\x1b[31m", "\x1b[32m", "\x1b[34m", "\x1b[33m", "\x1b[35m"};
	constexpr int MAX_VALUE = 13;
	constexpr card_t FOOL = MAGIC | 0;
	constexpr card_t WIZZARD = MAGIC | (MAX_VALUE*2+1);

	static constexpr card_t COLOR_NONE  = MAGIC; // no trick/trumpcolor means card was a wizzard (fool) -> color will match (be Magic)
	static constexpr card_t COLOR_UNSET = 0b10000001; // trick color not yet determend -> magic + 1

	inline card_t from_color_and_value(card_t col, card_t val) {
		return col | val;
	}
	inline card_t get_color(card_t c) {
		return c & 0b11100000;
	}
	inline bool is_magic(card_t c) {
		return c & MAGIC;
	}
	inline size_t get_index_from_color(card_t c) {
		return c >> 5;
	}
	inline card_t get_color_from_index(size_t i) {
		return i << 5;
	}
	inline card_t get_value(card_t c) {
		return c & 0b00011111;
	}
	inline string get_name(card_t c) {
		if (c == Card::WIZZARD) return "Wizzard";
		if (c == Card::FOOL) return "Fool";
		size_t i = get_index_from_color(get_color(c));
		return color_names[i] + to_string(get_value(c));
	}
	inline string get_colored_name(card_t c) {
		size_t i = get_index_from_color(get_color(c));
		return string(color_display_prefix[i]) + get_name(c) + "\x1b[0m";
	}
	inline string get_colored_color(card_t c) {
		size_t i = get_index_from_color(get_color(c));
		return string(color_display_prefix[i]) + color_names[i] + "\x1b[0m";
	}

	inline size_t get_card_index(card_t c) {
		if (c == FOOL) return 0;
		if (c == WIZZARD) return MAX_VALUE * 4 + 1;
		return get_index_from_color(get_color(c)) * MAX_VALUE + get_value(c);
	}

	int get_eval_value(card_t c, card_t trick_color, card_t trump_color) {
		card_t color = get_color(c);
		card_t value = get_value(c);
		if (color == trump_color && trump_color != Card::COLOR_NONE) return value + MAX_VALUE;
		if (color == trick_color || color == MAGIC) return value;
		if (trick_color == Card::COLOR_UNSET || trick_color == Card::COLOR_NONE) return value;
		return 0;
	}

	inline bool is_better(card_t card, card_t comparison, card_t trick_color, card_t trump_color) {
		assert(card != Card::EMPTY);
		if (comparison == Card::EMPTY) return true;
		return get_eval_value(card, trick_color, trump_color) > get_eval_value(comparison, trick_color, trump_color);
	}
	// use to slightly prefer higher value cards
	inline bool is_better_biased_prio_value(card_t card, card_t comparison, card_t trick_color, card_t trump_color)
	{
		assert(card != Card::EMPTY);
		if (comparison == Card::EMPTY) return true;
		return
			get_eval_value(card, trick_color, trump_color) * 100 + get_value(card) >
			get_eval_value(comparison, trick_color, trump_color) * 100 + get_value(comparison);
	}
	// use only for cosmetic sorting
	inline bool is_better_biased_prio_color(card_t card, card_t comparison, card_t trick_color, card_t trump_color)
	{
		assert(card != Card::EMPTY);
		if (comparison == Card::EMPTY) return true;
		return
			get_eval_value(card, trick_color, trump_color) * 1000 + get_color(card) * 100 + get_value(card) >
			get_eval_value(comparison, trick_color, trump_color) * 1000 + get_color(comparison) * 100 + get_value(comparison);
	}
};
using Card::card_t;

struct Hand {
	int m_id;
	std::vector<card_t> m_cards_arr;
	int m_cards_in_game_count;
	int m_player_count;
	card_t m_trump_color;
	// using uint64_t would be more efficient but due to marginal use improvement should be neglectable
	static constexpr size_t PROFILE_SIZE = Card::MAX_VALUE * 4 + 2; // 13*4 for normal cards; +2 for magic cards
	using profile_t = std::array<bool, Hand::PROFILE_SIZE>;
	
	Hand() = default;
	Hand(int id, int player_count, card_t trump_color, int starting_card_count) : m_id(id), m_player_count(player_count), m_trump_color(trump_color) {
		m_cards_arr.reserve(starting_card_count);
		m_cards_in_game_count = starting_card_count;
	}

	inline string get_name() const { return "Player" + to_string(m_id); }

	inline bool is_done() {return m_cards_in_game_count == 0;}

	profile_t get_profile() {
		profile_t p = {false};
		for (auto c : m_cards_arr) p[Card::get_card_index(c)] = true;
		return p;
	}

	void play_card(size_t index) {
		assert(index < m_cards_in_game_count && "tried to play card that is not in game");
		m_cards_in_game_count--;
		if (index == m_cards_in_game_count) return; // no need to swap since index is already the last card
		card_t played_card = m_cards_arr[index];
		m_cards_arr[index] = m_cards_arr[m_cards_in_game_count];
		m_cards_arr[m_cards_in_game_count] = played_card;
	}
	
	void unplay_last_card_to(size_t index) {
		assert(m_cards_in_game_count <= m_cards_arr.size() && "unplay failed");
		card_t played_card = m_cards_arr[m_cards_in_game_count];
		m_cards_arr[m_cards_in_game_count] = m_cards_arr[index];
		m_cards_arr[index] = played_card;
		m_cards_in_game_count++;
	}

	// returns an array of indecies of the cards that are reasonalbe to play
	vector<size_t> get_reasonable_cards(card_t trick_color, card_t current_winning_card) const {
		bool has_trick_color = false;
		if (trick_color != Card::COLOR_NONE && trick_color != Card::COLOR_UNSET) {
			for (size_t i=0; i<m_cards_in_game_count; i++) {
				if (Card::get_color(m_cards_arr[i]) == trick_color) {has_trick_color = true; break;}
			}
		}

		const size_t CARD_INDEX_UNSET = 100;

		enum ReasonableCardType {
			LOSING_RED,
			LOSING_GREEN,
			LOSING_BLUE,
			LOSING_YELLOW,
			HIGH_RED,
			HIGH_GREEN,
			HIGH_BLUE,
			HIGH_YELLOW,
			LOSING_FOOL,
			WINNING_HIGH,
			WINNING_LOW
		};

		const size_t STATE_COUNT = 11;
		size_t reasonable_cards[STATE_COUNT];
		std::fill(std::begin(reasonable_cards), std::end(reasonable_cards), CARD_INDEX_UNSET);

		auto is_better = [this, trick_color](card_t card, card_t comparison) {
			return Card::is_better_biased_prio_value(card, comparison, trick_color, m_trump_color);
		};

		auto card_from_category = [this, &reasonable_cards, CARD_INDEX_UNSET](size_t cat) -> card_t {
			return reasonable_cards[cat] == CARD_INDEX_UNSET
				? Card::EMPTY
				: m_cards_arr[reasonable_cards[cat]];
		};

		for (size_t card_index=0; card_index<m_cards_in_game_count; card_index++) {
			card_t card = m_cards_arr[card_index];
			card_t color = Card::get_color(card);
			// if trick color can be followed all other cards (except magic) are discarded
			if (has_trick_color && !Card::is_magic(card) && color != trick_color) {continue;}

			// first card
			if (current_winning_card == Card::EMPTY) {
				if (card == Card::FOOL) reasonable_cards[LOSING_FOOL] = card_index; // losing fool
				if (card == Card::WIZZARD) reasonable_cards[WINNING_HIGH] = card_index; // winning high -> wizzard
				if (Card::is_magic(card)) continue; // dont allow further logic to happen if magic

				size_t index = Card::get_index_from_color(color);

				if (card_from_category(index) == Card::EMPTY ||
					!is_better(card, card_from_category(index)))
					reasonable_cards[index] = card_index; // low color

				if (card_from_category(index + 4) == Card::EMPTY ||
					is_better(card, card_from_category(index + 4)))
					reasonable_cards[index + 4] = card_index; // high color
			}
			// not first card
			else {
				bool is_winning = is_better(card, current_winning_card);

				// winning
				if (is_winning) {
					if (card_from_category(WINNING_HIGH) == Card::EMPTY ||
						is_better(card, card_from_category(WINNING_HIGH)))
						reasonable_cards[WINNING_HIGH] = card_index; // winning high
					if (card_from_category(WINNING_LOW) == Card::EMPTY ||
							!is_better(card, card_from_category(WINNING_LOW)))
						reasonable_cards[WINNING_LOW] = card_index; // winning low
				}
				// not winning
				else {
					if (card == Card::FOOL)
						reasonable_cards[LOSING_FOOL] = card_index; // losing fool
					else {
						size_t index = Card::get_index_from_color(color);
						if (card_from_category(index) == Card::EMPTY ||
							!is_better(card, card_from_category(index)))
							reasonable_cards[index] = card_index; // losing low color
						if (card_from_category(index + 4) == Card::EMPTY ||
							is_better(card, card_from_category(index + 4)))
							reasonable_cards[index + 4] = card_index; // losing high color -> only for intentionally loosing
					}
				}
			}
		}

		vector<size_t> out;
		out.reserve(STATE_COUNT);
		bool seen[20] = {false};

		for (size_t i = 0; i < STATE_COUNT; i++) {
			size_t idx = reasonable_cards[i];
			if (idx != CARD_INDEX_UNSET && !seen[idx]) {
				out.push_back(idx);
				seen[idx] = true;
			}
		}
		assert(out.size() != 0 && "playable cards size can never be 0");

		return out;
	}
};

struct Trick {
	vector<card_t> m_cards;
	card_t m_color;
	int m_player_count;
	int m_starting_player;
	int m_current_player;
	size_t m_current_winning_player;

	Trick() {}

	void clear(int starting_player) {
		m_color = Card::COLOR_UNSET;
		m_starting_player = starting_player;
		m_current_player = starting_player;
		m_current_winning_player = starting_player;
	}

	Trick(int starting_player, int player_count) : m_player_count(player_count) {
		m_cards.assign(player_count, 0);
		clear(starting_player);
	}

	inline card_t get_current_winning_card() {
		if (m_current_player == m_starting_player) return Card::EMPTY; // befor the starting player there is no winning card
		return m_cards[m_current_winning_player];
	}

	inline bool is_done() {
		return m_current_player == m_starting_player;
	}

	void play(card_t card, card_t trump_color) {
		m_cards[m_current_player] = card;

		// update trick color
		if (m_color == Card::COLOR_UNSET && !Card::is_magic(card)) {
			m_color = Card::get_color(card);
		}
		if (card == Card::WIZZARD) {
			m_color = Card::COLOR_NONE;
		}

		// update winning player
		if (card != Card::FOOL) {
			card_t winning_card = get_current_winning_card();
			if (Card::is_better(card, winning_card, m_color, trump_color)) {
				m_current_winning_player = m_current_player;
			}
		}

		// increase current player
		m_current_player = (m_current_player+1) % m_player_count;
	}
};

struct Trick_cycle
{
	vector<int> m_score;
	vector<vector<card_t>> history;
	vector<float> m_score_distribution;
	int variance = 0;
	int call_count = 1;
	bool m_dummy = false;

	Trick m_trick;

	Trick_cycle() {}
	
	static Trick_cycle bad_dummy(int player_count) {
		Trick_cycle t;
		t.m_dummy = true; 
		t.m_score.assign(player_count, -1);
		t.m_score_distribution.assign(player_count, -1);
		return t;
	}

	Trick_cycle(int starting_player, int player_count) : m_trick(starting_player, player_count) {
		m_score.assign(player_count, 0);
		m_score_distribution.assign(player_count, 0.0f);
	}

	inline int get_current_player() {
		assert(!m_dummy && "dummy can not be accessed");
		return m_trick.m_current_player;
	}

	inline card_t get_card_by_player(int player) {
		assert(!m_dummy && "dummy can not be accessed");
		return m_trick.m_cards[player];
	}

	inline int get_winner() {
		assert(!m_dummy && "dummy can not be accessed");
		return m_trick.m_current_winning_player;
	}

	inline card_t get_leading_color() {
		assert(!m_dummy && "dummy can not be accessed");
		return m_trick.m_color;
	}

	void play_and_evaluate(card_t card, card_t trump_color) {
		assert(!m_dummy && "dummy can not be accessed");

		m_trick.play(card, trump_color);

		if (!m_trick.is_done()) return; // trick ongoing
		
		int winner = get_winner();

		// update score
		m_score[winner]++;
		m_score_distribution[winner]+= 1.0f;

		// expand history
		history.emplace_back();
		auto& h = history.back();
		h.push_back(Card::from_color_and_value(Card::RED, winner)); // winner
		h.push_back(Card::from_color_and_value(m_trick.m_color, 0)); // trick color
		h.insert(h.end(), m_trick.m_cards.begin(), m_trick.m_cards.end());

		// reset trick
		m_trick.clear(winner);
	}
};


struct Round {
	size_t m_player_count;
	size_t m_card_count;
	card_t m_trump_card;
	card_t m_trump_color;
	vector<Hand> m_player_hands_arr;
	int trick_count = 0;
	const size_t MAX_CARD_COUNT = 13*4 + 4*2; // 13 per color * 4 color + 4 Fools + 4 Wizzards
	float unpredictability_accountability = 0;

	Round(int player_count, int card_count) : m_player_count(player_count), m_card_count(card_count) {full();}

	void display() {
		info(string("The Trump card is ") + Card::get_colored_name(m_trump_card));
		info(string("The Trump color therefore is ") + (Card::is_magic(m_trump_card) ? string("not relevant") : Card::get_colored_color(m_trump_color)));

		for (auto& player : m_player_hands_arr) {
			info();
			info(string("~~~~~~ ") + player.get_name() + " ~~~~~~");
			info();
			info("Hand: ", true);
			for (size_t i = 0; i < player.m_cards_arr.size(); ++i) {
				if (i) info(", ", true);;
				cout << Card::get_colored_name(player.m_cards_arr[i]);
			}
			info();
		}
	}

	// needs full random hand, one hand given, one hand given + colors out
	void full() {
		// make deck
		card_t cards[MAX_CARD_COUNT];
		for (size_t i = 0; i < 4; i++) { // for each color
			for (size_t j = 0; j < Card::MAX_VALUE; j++) { // for each value
				cards[i*Card::MAX_VALUE+j] = Card::from_color_and_value(Card::get_color_from_index(i), j+1);
			}
		}
		for (int i = 0; i < 4; ++i) { 
			cards[4*Card::MAX_VALUE + i] = Card::FOOL;
			cards[4*Card::MAX_VALUE + 4 + i] = Card::WIZZARD;
		}

		// shuffle deck
		shuffle(cards, cards + MAX_CARD_COUNT, random_seed);

		// pick trump card
		m_trump_card = cards[0];
		m_trump_color = Card::get_color(m_trump_card); // * for Magic card trump_color is Card::COLOR_NONE
		// TODO: there is a special case for fool where the first player gets to pick the trump color (or that there is none)
		
		// create players and distribute cards
		for (int i = 0; i < (int)m_player_count; i++) {
			Hand hand(i, m_player_count, m_trump_color, m_card_count);
			for (int j = 0; j < m_card_count; j++) {
				hand.m_cards_arr.push_back(cards[1 + m_card_count*i + j]); // add one to not reuse trump
			}
			m_player_hands_arr.push_back(hand);
		}
	}

	Trick_cycle minimax_round(Trick_cycle& game, vector<Hand>& player_arr, const vector<int>& target) {
		Hand& p = player_arr[game.get_current_player()];
		if (p.is_done()) {
			return game;
		}
		card_t current_winning_card = game.get_card_by_player(game.get_winner());
		vector<size_t> card_indecies = p.get_reasonable_cards(game.get_leading_color(), current_winning_card);

		int call_count = 0; // used for profiling
		Trick_cycle dummy = Trick_cycle::bad_dummy(m_player_count); // -> empty; will be replaced
		Trick_cycle& best_trick = dummy;
		float best_score = INFINITY; // score gets minimized

		int possible_scenarios = 0;
		vector<float> new_score_distribution;
		new_score_distribution.assign(m_player_count, 0.0f);

		for (size_t i = 0; i < card_indecies.size(); i++) {
			size_t card_index = card_indecies[i];
			card_t card = p.m_cards_arr[card_index];

			Trick_cycle updated_game = game; // copy
			// TODO: strip history
			// TODO: do play unplay logic
			updated_game.play_and_evaluate(card, m_trump_color);

			p.play_card(card_index);

			Trick_cycle eval = minimax_round(updated_game, player_arr, target);

			p.unplay_last_card_to(card_index);

			call_count+= eval.call_count;

			float score = std::abs(eval.m_score_distribution[p.m_id] - (float)target[p.m_id]);
			if (score < best_score) {
				// needs second if to fire aswell for proper functionality
				best_trick = eval;
				best_score = score;
				new_score_distribution.assign(m_player_count, 0.0f);
				possible_scenarios = 0;
			}
			if (score - unpredictability_accountability <= best_score) {
				for (size_t i = 0; i < m_player_count; i++) new_score_distribution[i]+= eval.m_score_distribution[i];
				possible_scenarios++;
			}
			// break for perfect score -> no further investigation needed
			// potentially messes with probability for other players
			// only benefits if perfect score is possible
			// if (score == 0) break;
		}
		assert(!best_trick.m_dummy && "dummy trick was never replaced");

		for (size_t i = 0; i < m_player_count; i++) new_score_distribution[i] = new_score_distribution[i] / (float)possible_scenarios;

		best_trick.m_score_distribution = new_score_distribution;

		best_trick.call_count = call_count;
		return best_trick;
	}

	void eval_timed(float unpredictability_accountability) {
		vector<int> target(m_player_count, m_card_count);

		info(" --------------- Test round --------------- ");

		display();

		info();
		info(to_string(unpredictability_accountability));
		auto start = std::chrono::high_resolution_clock::now();
		Trick_cycle trick(0, m_player_count);
		Trick_cycle result = minimax_round(trick, m_player_hands_arr, target);
		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
		info(string("Execution time: ") + to_string(duration.count()) + "ms");
		info(string("Call count: ") + to_string(result.call_count));
		
		for (const auto& x : result.m_score_distribution) std::cout << x << ' ';
		cout << std::endl;

		info("expected tricks: ", true);
		for (const auto& x : result.m_score) std::cout << x << ' ';
		cout << std::endl;
		for (const auto& x : result.history) {
			for (const auto& y : x) std::cout << Card::get_colored_name(y) << ' ';
			cout << std::endl;
		}
		info();
		info();
	}

	void run(int starting_player) {
		eval_timed(0.0f);
	}
};

struct Game {
	vector<int> score;

	void test(int starting_player) {
		const int runs = 100;
		const int player_count = 3;
		const int card_count = 4;
		int call_cout = 0;
		vector<int> target(player_count, card_count);
		auto start = std::chrono::high_resolution_clock::now();
		for (size_t i=0; i<runs; i++) {
			std::cout << "\rCycle: " << i << std::flush;
			Round r(player_count, card_count);
			
			Trick_cycle trick(starting_player, player_count);
			Trick_cycle result = r.minimax_round(trick, r.m_player_hands_arr, target);
			call_cout+= result.call_count;
		}
		std::cout << "\r" << std::flush;
		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

		info(" - average performace test -");
		info("average time per run (ms):");
		info(to_string(duration.count()/runs));
		info("average time per 1.000 calls (ms):");
		info(to_string(duration.count()*1000/(long long)call_cout));
		info("average calls per run:");
		info(to_string(call_cout/runs));
	}

	Game(int player_count) {
		test(0);
		Round round(3, 4);
		round.run(0);
	}
};

int main() {
	ios::sync_with_stdio(false); // speeds up cout
	Game g(2);
	return 0;
}
