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
#pragma once

#ifndef MIGRATION_LALLIANCE_H
#define MIGRATION_LALLIANCE_H


//#include "..\\autonomic\\autonomic.h"

#include "..\\autonomic\\DDB.h"
#include <string>
#include <vector>
#include <random>
#include <map>

typedef mapTask taskList;
typedef DDBTask * TASK;
//---------------------------------------------------------
class AgentTeamLearning;	//Forward declaration due to circular dependencies of LAlliance and AgentTeamLearning



class LAlliance {

public:

    // Algorithm variables
    unsigned int maxTaskTime;        // Max allowed time on task
    unsigned int motivFreq;          // Frequency at which motivation updates
    float impatienceRateTheta;       // Coefficient for impatience rate calculation
    float stochasticUpdateTheta2;    // Coefficient for stochastic update
    float stochasticUpdateTheta3;    // Coefficient for stochastic update
    float stochasticUpdateTheta4;    // Coefficient for stochastic update

    // Separate parameter for max task time, enable it to be adjusted
    unsigned int delta;

    //Pointer to parent agent

    AgentTeamLearning *parentAgent;


    //NilUUID for comparison
    UUID nilUUID;

    // This avatar's Id and tracking metrics
    UUID id;
    DDBTaskData myData;

    // Info about other avatars tracking metric
    std::map<UUID, DDBTaskData, UUIDless> teammatesData;



    //-----------------------------------------
    // Member methods

    // Constructor
    LAlliance(UUID *idIn);
    LAlliance(AgentTeamLearning *parentAgent);

    /* addTask
     *
     * To be called when a new task is discovered. A new element in the tau,
     * motivation, impatience, and attempts maps will be created.
     */
    int addTask(UUID id);


    /* updateTaskProperties
     *
     * Performs all the necessary updates to the tracking metrics. Must be
     * called before chooseTask.
     */
    int updateTaskProperties(const taskList &tasks);


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
    int chooseTask(const taskList &tasks);


    /* updateImpatience
     *
     * Determines the impatience rate for the avatar towards each task.
     * The rates are calculated as described in [Lynne Parker, 1998],
     * with the modification from [Girard, 2015] for pareto-optimal
     * task selection.
     *
     * Impatience is set to zero for completed tasks and the assigned task.
     */
    int updateImpatience(const taskList &tasks);


    /* updateMotivation
     *
     * Increments the avatars motivation towards each task, according
     * to it's impatience.
     *
     * Updated according to Equation (1) in [Lynne Parker, 1998]
     */
    int updateMotivation(const taskList &tasks);


    /* finishTask
     *
     * To be used when a task has been completed. It will reset the tacking
     * metrics and task Id.
     */
    int finishTask();


    /* acquiesce
     *
     * Unassign the task from this robot, and zero all tracking metrics.
     * To be used when the task has been attempted for too long, or another
     * avatar has taken over the task
     */
    int acquiesce(UUID id);


    /* updateTau
     *
     * Updates the average trial time with a recursive stochastic update based
     * on the time spent on a specific task [Girard, 2015]
     */
    int updateTau();


    /* requestAcquiescence
     *
     * Sends a message to the avatar indicated by id that it must acquiesce
     * its current task as a result of this avatar taking over the task.
	 * taskId is the task to be acquiesced, used when the receiving agent has already changed tasks
     */
    int requestAcquiescence(UUID taskId, UUID id);


    /* motivationReset
     *
     * Zeros the avatars motivation towards task [id]. For use when another avatar
     * attempts a new task for the first time.
     */
    int motivationReset(UUID id);


    /* requestMotivationReset
     *
     * To be called at the beginning of an avatar's first attempt at a new task.
     * Notifies all other avatars to zero their motivation towards this task.
     */
    int requestMotivationReset(UUID id);

};

#endif //MIGRATION_LALLIANCE_H