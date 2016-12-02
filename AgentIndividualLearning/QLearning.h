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

#ifndef QLEARNING_H
#define QLEARNING_H

#include <vector>
#include <math.h>
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
    std::vector<unsigned int> state_resolution_ ;               // Bits required to express state values
    unsigned int num_actions_;               // Number of possible actions
    std::vector<unsigned int> encoder_vector_;
    unsigned int table_size_;               // Length of Q-table
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