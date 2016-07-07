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

#ifndef QLEARNING_H
#define QLEARNING_H

#include <vector>
//#include <math.h>
#include <algorithm>

class QLearning {

public:

    // Q-Learning algorithm parameters
    float gamma_;
    float alpha_denom_;
    float alpha_power_;
    float alpha_max_;

    // Quality/Experience table
    unsigned int num_state_vrbls_;           // Number of variables in state vector
    unsigned int state_bits_ ;               // Bits required to express state values
    unsigned int num_actions_;               // Number of possible actions
    unsigned  int action_bits_;              // Bits required to express action number
    unsigned  int table_size_;               // Length of Q-table
    std::vector<float> q_table_;             // Vector of all Q-values
    std::vector<unsigned int> exp_table_;    // Vector of all experience values
    std::vector<float> q_vals_;              // Vector of current Q-values
    std::vector<unsigned int> exp_vals_;     // Vector of current experience values

    // Methods

    // Constructor
    QLearning();

    void learn(std::vector<unsigned int> &state_now, std::vector<unsigned int> &state_future, int &action_id, float &reward);

    void storeElements(std::vector<unsigned int> &state_vector, float &quality, int &action_id);

    void getElements(std::vector<unsigned int> &state_vector);

    void reset();

    int getKey(std::vector<unsigned int> &state_vector, int &action_id);


private:

};// end QLearning

#endif // QLEARNING_H
