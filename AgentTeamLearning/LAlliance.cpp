/* LAlliance - L-Alliance algorithm for task allocation
 *
 * Based off the algorithm presented in [Lynne Parker, 1998], with
 * additional modifications described in [Girard, 2015].
 *
 * Monitors the performance of all agents towards each tasks, then uses
 * those beliefs to assign new tasks to agents, and determine when to
 * give up on a task.
 *
 */


//---------------------------------------
// TODO

/* Add functionality to broadcast, and receive the task and avatar data
 *      -Receive must merge in new avatar and task data
 *      -Receive must be called before updateTaskProperties
 *      -Broadcast must send out this avatars data, and updates to task data
 *      -Broadcast must be called after chooseTask
 *      -Broadcast must be called after being forced to acquiesce
 */
//---------------------------------------


#include "stdafx.h"
#include "LAlliance.h"
#include "AgentTeamLearning.h"
#include "AgentTeamLearningVersion.h"

// Constructor
//LAlliance::LAlliance(UUID *idIn) {
//
//    // Algorithm variables (TODO: Load these in from a config file)
//    maxTaskTime = 500;
//    motivFreq = 5;
//    impatienceRateTheta = 1.0;
//    stochasticUpdateTheta2 = 0.9;
//    stochasticUpdateTheta3 = 1.0;
//    stochasticUpdateTheta4 = 2.5;
//
//    // Initialize variables
//   // id = idIn;
//    myData.psi = 0;
//    delta = maxTaskTime;    // Default to this
//
//	UuidCreateNil(&this->nilUUID);
//	// uuid
//
//	if (*idIn == this->nilUUID)
//		UuidCreate(&id);
//	else
//		id = *idIn;
//}

LAlliance::LAlliance(AgentTeamLearning *parentAgent) {

    //// Algorithm variables (TODO: Load these in from a config file)
    maxTaskTime = 2000;
    motivFreq = 5;
    impatienceRateTheta = 1.0f;
    stochasticUpdateTheta2 = 15.0f;
    stochasticUpdateTheta3 = 0.3f;
    stochasticUpdateTheta4 = 2.0f;

    //// Initialize variables
    //// id = idIn;
    myData.psi = 0;
    delta = maxTaskTime;    // Default to this

    UuidCreateNil(&this->nilUUID);
    //// uuid
    UuidCreate(&this->id);

    myData.taskId = this->nilUUID;
    this->parentAgent = parentAgent;

    UuidCreateNil(&this->myData.agentId);

    srand((unsigned)time(NULL));

}

/* addTask
 *
 * To be called when a new task is discovered. A new element in the tau,
 * motivation, impatience, and attempts maps will be created.
 */

int LAlliance::addTask(UUID id) {

    if (id == nilUUID) {
        return 1;
    }

    // Add the task to the DDBTaskData struct
    myData.motivation.insert(std::pair<UUID,float>(id, 0));
    myData.impatience.insert(std::pair<UUID, float>(id, 0));
    myData.attempts.insert(std::pair<UUID, int>(id, 0));

    // Initialize tau as the max task time, since it converges quickly, and add
    // noise since identical taus will cause problems in the impatience calculation
    // TODO: Replace this with Boost Random generator in HAA?
    float low = 0.9f*0.5f*maxTaskTime;
    float high = 1.1f*0.5f*maxTaskTime;
    myData.tau.insert(std::pair<UUID, int>(id, low + static_cast <float> (rand()) /( static_cast <float> (RAND_MAX/(high - low)))));

    return 0;
}


/* updateTaskProperties
 *
 * Performs all the necessary updates to the tracking metrics. Must be
 * called before chooseTask.
 */

int LAlliance::updateTaskProperties(const taskList &tasks) {

    // Increment time on task
    if (myData.taskId != nilUUID) {
        myData.psi += motivFreq;
    }

    // Check if the task should be acquiesced
    if (myData.psi >= delta) {
        acquiesce(myData.taskId);
    }

    // Update impatience and motivation
    updateImpatience(tasks);
    updateMotivation(tasks);

    return 0;
}


/* chooseTask
 *
 * Selects potential tasks based on the following requirements:
 *      - No other avatars are assigned to the task, or they have been
 *        engaged in the task long enough to acquiesce (their tau plus one
 *        standard deviation)
 *      - This avatar is the most motivated
 *
 * From the potential tasks, if this avatar is expected to be the best
 * (lowest tau) then the longest available task will be selected.
 * Otherwise the shortest task will be selected.
 *
 * If no potential tasks are found, a nilUUID will be assigned
 */

int LAlliance::chooseTask(const taskList &tasks) {

    // No need to choose a task if one is assigned
    if (myData.taskId != nilUUID) {
        return 0;
    }

    std::map<UUID, float, UUIDless> category1;
    std::map<UUID, float, UUIDless> category2;

    // Go through each incomplete task, and assign it to category 1 or 2
    // Category 1: This avatar is expected to be the best at this task
    // Category 2: Another avatar is expected to be the best at this task
    std::map<UUID, TASK, UUIDless>::const_iterator taskIter;
    for (taskIter = tasks.begin(); taskIter != tasks.end(); taskIter++) {

        // Tasks are considered available when they are incomplete, and either
        // no avatar is assigned, or the assigned avatar has been attempting
        // the task for longer than their tau value
        bool available = true;
        if (taskIter->second->avatar == nilUUID) {
            // No avatar assigned, but must check if it is completed
            available = !taskIter->second->completed;
        }
        else if (teammatesData.find(taskIter->second->avatar) != teammatesData.end()) {//Check, otherwise the map will be inserted with incorrect values
            if (teammatesData[taskIter->second->avatar].psi < (teammatesData[taskIter->second->avatar].tau[taskIter->first]
                                                               + teammatesData[taskIter->second->avatar].stddev[taskIter->first])) {
                // An avatar is assigned, but it has not been engaged long enough to acquiesce
                available = false;
            }
        }

        if (available) {
            parentAgent->logWrapper(" chooseTask: Available.");
            // Check each avatar
            bool fastest = true;
            bool mostMotivated = true;
            std::map<UUID, DDBTaskData, UUIDless>::iterator avatarIter;
            if (teammatesData.empty() == false) {	//Check, otherwise the map will be inserted with incorrect values
                for (avatarIter = teammatesData.begin(); avatarIter != teammatesData.end(); avatarIter++) {

                    // Check if another avatar is expected to be faster
                    if (avatarIter->second.tau[taskIter->first] < myData.tau[taskIter->first]) {
                        fastest = false;
                    }

                    // Check if another avatar is more motivated
                    if (avatarIter->second.motivation[taskIter->first] > myData.motivation[taskIter->first]) {
                        mostMotivated = false;
                        parentAgent->logWrapper(" chooseTask: another is more motivated...");
                    }

                }
            }

            if (mostMotivated) {
                if (fastest) {
                    // Expected to be the best, so assign to category 1
                    category1[taskIter->first] = myData.tau[taskIter->first];
                } else {
                    // Another avatar is expected to be better, so assign to category 2
                    category2[taskIter->first] = myData.tau[taskIter->first];
                }

            }
        }
    }

    // Take the longest task from category 1, or if no tasks belong to category 1
    // take the shortest task from category 2
    UUID taskAssignment = this->nilUUID;
    //int taskAssignment = 0;
    if (category1.size() != 0) {
        // There is a task this avatar is expected to be the best at
        // Find the longest task in this category
        float longestTime = 0;
        std::map<UUID, float, UUIDless>::iterator cat1Iter;
        for (cat1Iter = category1.begin(); cat1Iter != category1.end(); cat1Iter++) {
            if (cat1Iter->second > longestTime) {
                longestTime = cat1Iter->second;
                taskAssignment = cat1Iter->first;
                parentAgent->logWrapper(" chooseTask: cat1...");
            }
        }

    } else if (category2.size() != 0) {
        // This avatar is not expected to be the best at any available task,
        // Find the shortest task in this category
        float shortestTime = 9999999;
        std::map<UUID, float, UUIDless>::iterator cat2Iter;
        for (cat2Iter = category2.begin(); cat2Iter != category2.end(); cat2Iter++) {
            if (cat2Iter->second < shortestTime) {
                shortestTime = cat2Iter->second;
                taskAssignment = cat2Iter->first;
                parentAgent->logWrapper(" chooseTask:cat2...");
            }
        }
    }

    // Assign the task
    myData.taskId = taskAssignment;

    if (taskAssignment != nilUUID) {
        //parentAgent->logWrapper(" chooseTask: We dont reach here...");
        if (myData.attempts[taskAssignment] == 0) {
            // This is the first attempt, zero other avatar's motivation
            requestMotivationReset(taskAssignment);
        }
    }
    // Increment the task attempts
    myData.attempts[taskAssignment]++;

    if (taskAssignment != nilUUID && tasks.at(taskAssignment)->avatar != nilUUID) {
        parentAgent->logWrapper(" chooseTask: requesting acquiescence...");
        // Another avatar was assigned, and they must acquiesce
        requestAcquiescence(tasks.at(taskAssignment)->agentUUID);	//Send directly to the team learning agent, not the avatar agent
    }

    return 0;
}


/* updateImpatience
 *
 * Determines the impatience rate for the avatar towards each task.
 * The rates are calculated as described in [Lynne Parker, 1998],
 * with the modification from [Girard, 2015] for pareto-optimal
 * task selection.
 *
 * Impatience is set to zero for completed tasks and the assigned task.
 */

int LAlliance::updateImpatience(const taskList &tasks) {

    // Loop through each task, since impatience rates vary
    std::map<UUID, TASK, UUIDless>::const_iterator taskIter;
    for (taskIter = tasks.begin(); taskIter != tasks.end(); taskIter++){

        // Don't update for completed tasks
        if (taskIter->second->completed) {
            myData.impatience[taskIter->first] = 0;
            break;
        }

        // Only use the slow update rate [Girard, 2015]
        // Only update for tasks not assigned to this avatar
        if (taskIter->second->avatar != id) {
            // Avatar assigned to task, so grow at slow rate
            myData.impatience[taskIter->first] = impatienceRateTheta / myData.tau[taskIter->first];
        } else {
            myData.impatience[taskIter->first] =0;
        }

    }
    return 0;
}


/* updateMotivation
 *
 * Increments the avatars motivation towards each task, according
 * to it's impatience.
 *
 * Updated according to Equation (1) in [Lynne Parker, 1998]
 */

int LAlliance::updateMotivation(const taskList &tasks) {

    // Update for each task tasks
    std::map<UUID, TASK, UUIDless>::const_iterator iter;
    for (iter = tasks.begin(); iter != tasks.end(); iter++){
        // Only update for tasks that are not complete yet
        if (!iter->second->completed) {
            // Must account for update frequency
            myData.motivation[iter->first] += myData.impatience[iter->first] * motivFreq;
        } else {
            myData.motivation[iter->first] = 0;
        }
    }
    return 0;
}


/* finishTask
 *
 * To be used when a task has been completed. It will reset the tracking
 * metrics and task Id.
 */

int LAlliance::finishTask() {
    // Update the tau value
    updateTau();

    // Reset tracking metrics and assignment
    myData.motivation[myData.taskId] = 0;
    myData.impatience[myData.taskId] = 0;
    myData.taskId = nilUUID;
    myData.psi = 0;

    return 0;
}


/* acquiesce
 *
 * Unassign the task from this robot, and zero all tracking metrics.
 * To be used when the task has been attempted for too long, or another
 * avatar has taken over the task
 */

int LAlliance::acquiesce(UUID id) {
    // Update the tau value
    updateTau();

    // Reset tracking metrics and assignment
    myData.motivation[myData.taskId] = 0;
    myData.impatience[myData.taskId] = 0;
    myData.taskId = nilUUID;
    myData.psi = 0;

    return 0;
}


/* updateTau
 *
 * Updates the average trial time with a recursive stochastic update based
 * on the time spent on a specific task [Girard, 2015]
 */

int LAlliance::updateTau() {
    // Useful values
    int n = myData.attempts[myData.taskId];
    unsigned int time_on_task = myData.psi;

    // Extract the old value
    float prev_tau = myData.tau[myData.taskId];

    // Compute the new value
    float beta = (float)exp(n / stochasticUpdateTheta4)/(stochasticUpdateTheta3
                                                         + (float)exp(n / stochasticUpdateTheta4));
    float current_tau = beta*(prev_tau + (stochasticUpdateTheta2 / n)*(myData.psi - prev_tau));

    // Update the mean task time, and tau standard deviation
    float prev_mean = myData.mean[myData.taskId];
    float prev_stddev = myData.stddev[myData.taskId];
    float current_mean, current_stddev;
    if (n > 1) {
        current_mean = prev_mean + (time_on_task - prev_mean)/n;
        current_stddev = (float)sqrt((pow(n - 1, 2)*pow(prev_stddev, 2) + (time_on_task - prev_mean)*(time_on_task - current_mean))/n);
    }else {
        current_mean = prev_mean;
        current_stddev = prev_stddev;
    }

    // Save the new values
    myData.tau[myData.taskId] = current_tau;
    myData.mean[myData.taskId] = current_mean;
    myData.stddev[myData.taskId] = current_stddev;

    return 0;
}


/* requestAcquiescence
 *
 * Sends a message to the avatar indicated by id that it must acquiesce
 * its current task as a result of this avatar taking over the task.
 */

int LAlliance::requestAcquiescence(UUID agentId) {

    parentAgent->sendRequest(&agentId, AgentTeamLearning_MSGS::MSG_REQUEST_ACQUIESCENCE, &nilUUID);


    //DataStream lds;

    //UUID thread = parentAgent->conversationInitiate(AgentTeamLearning::AgentTeamLearning_CBR_convReqAcquiescence, DDB_REQUEST_TIMEOUT, &id, sizeof(UUID));
    //if (thread == nilUUID) {
    //	//AgentTeamLearning_MSGS::
    //	return 1;
    //}
    //lds.reset();
    //lds.packUUID(parentAgent->getUUID());			// Sender id
    //lds.packUUID(&thread);
    //parentAgent->sendAgentMessage(&id, AgentTeamLearning_MSGS::MSG_REQUEST_ACQUIESCENCE, lds.stream(), lds.length());
    //lds.unlock();
    return 0;
}


/* motivationReset
 *
 * Zeros the avatars motivation towards task [id]. For use when another avatar
 * attempts a new task for the first time.
 */

int LAlliance::motivationReset(UUID id) {
    std::map<UUID, float, UUIDless>::iterator testIt;
    testIt = myData.motivation.find(id);
    if (testIt != myData.motivation.end())
        myData.motivation[id] = 0;
    else
        parentAgent->logWrapper("HAHAHA");
    return 0;
}


/* requestMotivationReset
 *
 * To be called at the beginning of an avatar's first attempt at a new task.
 * Notifies all other avatars to zero their motivation towards this task.
 */

int LAlliance::requestMotivationReset(UUID id) {
    parentAgent->logWrapper("requestMotivationReset");
    DataStream lds;
    UUID agentId;

    std::map<UUID, DDBTaskData, UUIDless>::iterator avatarIter;
    if (teammatesData.empty() == false) {
        for (avatarIter = teammatesData.begin(); avatarIter != teammatesData.end(); avatarIter++) {

            agentId = avatarIter->second.agentId;

            parentAgent->sendRequest(&agentId, AgentTeamLearning_MSGS::MSG_REQUEST_MOTRESET, &id);

        }
    }

    return 0;
}

