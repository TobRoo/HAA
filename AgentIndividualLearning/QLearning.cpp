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
 * Example: There are 5 state variables, with minimum values of
 * [0, 0, 0, 0, 0] and maximum values of [15, 1, 15, 15, 15]
 *
 * A table is formed such that:
 *   Rows 1:80 are for vectors [0 0 0 0 0] to [15 0 0 0 0]
 *   Rows 81:160 are for vectors [0 1 0 0 0] to [15 1 0 0 0]
 *   Rows 161:2561 are for vectors [0 0 0 0 0] to [15 1 15 0 0]
 *   etc.
 *
 * An encoder vector is used, so that when the state vector is
 * multiplied by this vector, it accounts for the offset needed for each
 * element. For this example the encoder vector is
 * [1, 80, 160, 2560, 40960]. Thus the second element of the state
 * vector gets offset by 80, the third by 160, the forth by 2560, etc..
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
    this->alpha_denom_ = 300;
    this->alpha_power_ = 2;
    this->num_state_vrbls_ = 7;
    this->state_resolution_ = {3, 3, 5, 3, 5, 3, 5};
    this->num_actions_ = 5;

    // Form encoder vector to multiply inputted state vectors by
    this->encoder_vector_.push_back(this->num_actions_);
    unsigned int state_resolution_prod = this->state_resolution_[0]*this->num_actions_;
    for(int i = 1; i < this->num_state_vrbls_ ; i++) {
        this->encoder_vector_.push_back(state_resolution_prod);
        state_resolution_prod = state_resolution_prod*this->state_resolution_[i];
    }

    // Determine table size
    this->table_size_ = state_resolution_prod;

    // Initialize vectors
    this->q_table_.resize(this->table_size_, 0);
    this->exp_table_.resize(this->table_size_, 0);

    // Initialize current vals
    this->q_vals_.resize(this->num_actions_, 0);
    this->exp_vals_.resize(this->num_actions_, 0);

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
    std::vector<float> q_values_now = getElements(state_now);
    float quality_now = q_values_now[action_id - 1];
    unsigned int exp_now = exp_vals_[action_id - 1];

    // Get future quality
	std::vector<float> q_values_future = getElements(state_future);
    std::vector<float>::iterator quality_future = std::max_element(q_values_future.begin(), q_values_future.end());

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

std::vector<float> QLearning::getElements(std::vector<unsigned int> &state_vector) {

    // Find rows corresponding to state vector
    int uselessAction = 1;
    int row = getKey(state_vector, uselessAction);

    // Retrieve quality and experience
	std::vector<float> q_values;
    for (unsigned int i = 0; i < num_actions_; i++){
		q_values.push_back(this->q_table_[row + i]);
    }// end for

	return q_values;
}// end getQValue

std::vector<unsigned int> QLearning::getExpElements(std::vector<unsigned int> &state_vector) {

	// Find rows corresponding to state vector
	int uselessAction = 1;
	int row = getKey(state_vector, uselessAction);

	// Retrieve quality and experience
	std::vector<unsigned int> exp_values;
	for (unsigned int i = 0; i < num_actions_; i++) {
		exp_values.push_back(this->exp_table_[row + i]);
	}// end for

	return exp_values;
}// end getExpValue


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
    // Multiply state vector by the encoder vector and add the action number to convert to a
    // unique key value (key starts at zero)
    int key = (action_id - 1);
    for (int i = 0; i < this->num_state_vrbls_; i++) {
        key += state_vector[i]*this->encoder_vector_[i];
    }
    return key;
}// end getElements