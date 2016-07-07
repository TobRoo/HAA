/* QLearning - Performs Q-Learning given states, actions and rewards
 *
 * Contains equations for updating Q-values and the learning rate,
 * then stores the updated values in a SparseQTable.
 *
 * Also keeps track of alpha, gamma, experience, quality, and reward
 * at each iteration.
 *
 * A standard Q-learning update rule is used [Boutilier, 1999], with a
 * constant gamma, and an exponentially decreasing learning rate
 * dependent on the number of visitations to a state [Source Unknown].
 *
 * Given the number of state variables, the range of state variables,
 * and the number of actions, an appropriately sized vector will be formed.
 *
 * Each state and action combo will have a unique key value,
 * representing the corresponding row in the table.
 *
 * State variables and action values must be non-zero integers.
 *
 * Example: There are 5 state variables, with possible values between
 * 0 and 15, and there are 8 possible actions
 *
 * The state vector will look like: [X, X, X, X, X]
 * 3 Bits are needed to express the actions
 * 4 Bits are needed to express the state variables
 *
 * Rows of the data vector will be formed as follows:
 * Rows 1 to 2^(3+4) will be for state vectors [0 0 0 0 0] to [15 0 0 0 0]
 * Rows (2^(3+4)+1) to 2(3+4+4) will be for state vectors [0 1 0 0 0] to [15 1 0 0 0]
 * Rows (2^(3+4+4)+1) to 2(3+4+4+4) will be for state vectors [0 2 0 0 0] to [15 2 0 0 0]
 * etc.
 */

#include "stdafx.h"
#include "QLearning.h"

/* Constructor
 *
 * Determines table size, and initializes quality and experience
 */

QLearning::QLearning() {

    // TODO: The data below should be loaded in from a config file
    gamma_ = 0.3f;
	this->alpha_max_ = 0.9f;
	this->alpha_denom_ = 30;
	this->alpha_power_ = 1;
	this->num_state_vrbls_ = 5;
	this->state_bits_ = 4;
	this->num_actions_ = 4;

    // Min bits required for actions
	this->action_bits_ = (unsigned int) ceil(log2(num_actions_));

    // Determine table size
	this->table_size_ = (unsigned int) pow(2, (num_state_vrbls_ * state_bits_ + action_bits_));

    // Initialize vectors
	this->q_table_.resize(table_size_, 0);
	this->exp_table_.resize(table_size_, 0);

    // Initialize current vals
	this->q_vals_.resize(num_actions_, 0);
	this->exp_vals_.resize(num_actions_, 0);

}// end constructor


/* learn
 *
 * Performs Q-learning update, stores the learning data, and updates the quality table
 *
 * INPUTS
 * state_now =  Vector of current state variables
 * state_future = Vector of future state variables
 * action_id = Action number [1, num_actions]
 * reward = Reward received
*/

void QLearning::learn(std::vector<unsigned int> &state_now, std::vector<unsigned int> &state_future, int &action_id, float &reward) {
    // Get current qualities and experience
    getElements(state_now);
    float quality_now = q_vals_[action_id - 1];
    unsigned int exp_now = exp_vals_[action_id - 1];

    // Get future quality
    getElements(state_future);
    std::vector<float>::iterator quality_future = std::max_element(q_vals_.begin(), q_vals_.end());

    // Exponentially decrease learning rate with experience [Unknown]
    float alpha = this->alpha_max_/(float) exp(pow(exp_now, this->alpha_power_)/ this->alpha_denom_);

    // Standard Q-learning update rule [Boutilier, 1999]
    float quality_update = quality_now + alpha*(reward + (gamma_* *quality_future) - quality_now);

    // Update tables
    storeElements(state_now, quality_update, action_id);

}// end learn


/* getElements
 *
 * Retrieves quality and experience from table, and stores in member variables
 *
 * INPUTS
 * state_vector = Vector of state variables
 */

void QLearning::getElements(std::vector<unsigned int> &state_vector) {

    // Find rows corresponding to state vector
	int uselessAction = 1;
    int row = getKey(state_vector, uselessAction);

    // Retrieve quality and experience
    for (unsigned int i = 0; i < num_actions_; i++){
		this->q_vals_[i] = this->q_table_[row + i];
		this->exp_vals_[i] = this->exp_table_[row + i];
    }// end for

}// end getQValue


/* storeElements
 *
 * Stores a new quality value in the table, and updates experience
 *
 * INPUTS
 * state_vector = Vector of state variables
 * quality = new quality value
 * action_id = Action number [1,num_actions_]
 */

void QLearning::storeElements(std::vector<unsigned int> &state_vector, float &quality, int &action_id) {

    // Find corresponding row in table
    int key = getKey(state_vector, action_id);
    // Increment experience
    unsigned int experience = this->exp_table_[key] + 1;
    // Insert new data
	this->q_table_[key] = quality;
	this->exp_table_[key] = experience;

}// end storeElements


/* reset
 *
 * Resets the tables to vectors of zero
 */

void QLearning::reset() {
    std::fill(this->q_table_.begin(), this->q_table_.end(), 0.0f);
    std::fill(this->exp_table_.begin(), this->exp_table_.end(), 0);
}// end reset



/* getElements
 *
 * Get the table index for a certain key vector
 *
 * INPUTS
 * state_vector = Vector of state variables
 * action_id = Action number [1,num_actions_]
 */

int QLearning::getKey(std::vector<unsigned int> &state_vector, int &action_id) {

    // Increment key value for each entry in state_vector (see above for example)
    int key = action_id - 1;     // key starts at zero
    int shift;

    for (int i = 0; i < state_vector.size(); i++) {
        shift = state_vector[i] * (int) pow(2, (i * this->state_bits_ + this->action_bits_));
        key += shift;
    }// end for

    return key;

}// end getElements