#include "SmPreviewRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

int SmPreviewRuntime::StateIndex(const BrxSmRecord &machine, const std::int16_t oid)
{
    const auto found = std::find_if(machine.states.begin(), machine.states.end(),
                                    [&](const BrxSmStateRecord &state) { return state.oid == oid; });
    return found == machine.states.end() ? -1 : static_cast<int>(std::distance(machine.states.begin(), found));
}

void SmPreviewRuntime::Log(std::string message)
{
    trace_.push_back(std::move(message));
    if (trace_.size() > 64)
        trace_.erase(trace_.begin());
}

float SmPreviewRuntime::Random01()
{
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return static_cast<float>(randomState_ >> 8) / 16777216.0f;
}

void SmPreviewRuntime::Bind(const std::int32_t machineObjectIndex, const BrxSmRecord &machine,
                            const std::int32_t rootObjectIndex)
{
    snapshot_ = {};
    snapshot_.machineObjectIndex = machineObjectIndex;
    snapshot_.boundRootObjectIndex = rootObjectIndex;
    snapshot_.speed = 1.0f;
    trace_.clear();
    Reset(machine);
}

void SmPreviewRuntime::Reset(const BrxSmRecord &machine)
{
    snapshot_.activeTransitionIndex = -1;
    snapshot_.animationObjectIndex = -1;
    snapshot_.nextStateOid = -1;
    snapshot_.reverse = false;
    snapshot_.waitingForAnimation = false;
    snapshot_.currentStateOid = machine.states.empty() ? -1 : machine.states.front().oid;
    snapshot_.goalStateOid = snapshot_.currentStateOid;
    trace_.clear();
    Log("Reset to state OID " + std::to_string(snapshot_.currentStateOid));
}

void SmPreviewRuntime::FinishWithoutTransition()
{
    snapshot_.activeTransitionIndex = -1;
    snapshot_.animationObjectIndex = -1;
    snapshot_.nextStateOid = -1;
    snapshot_.reverse = false;
    snapshot_.waitingForAnimation = false;
}

bool SmPreviewRuntime::SetGoal(const BrxSmRecord &machine, const std::int16_t goalOid)
{
    if (StateIndex(machine, goalOid) < 0)
        return false;
    if (snapshot_.goalStateOid == goalOid)
        return true;
    snapshot_.goalStateOid = goalOid;
    Log("Goal set to OID " + std::to_string(goalOid));
    if (snapshot_.activeTransitionIndex >= 0 &&
        static_cast<std::size_t>(snapshot_.activeTransitionIndex) < machine.transitions.size() &&
        (machine.transitions[static_cast<std::size_t>(snapshot_.activeTransitionIndex)].flags & 2) != 0)
    {
        Log("Interrupted active transition after goal change");
        FinishWithoutTransition();
    }
    return snapshot_.waitingForAnimation || ChooseTransition(machine);
}

bool SmPreviewRuntime::Seek(const BrxSmRecord &machine, const std::int16_t stateOid)
{
    if (StateIndex(machine, stateOid) < 0)
        return false;
    FinishWithoutTransition();
    snapshot_.currentStateOid = stateOid;
    snapshot_.goalStateOid = stateOid;
    Log("Seeked directly to OID " + std::to_string(stateOid));
    return ChooseTransition(machine);
}

bool SmPreviewRuntime::Advance(const BrxSmRecord &machine)
{
    if (snapshot_.waitingForAnimation)
        return CompleteAnimatedTransition(machine);
    return ChooseTransition(machine);
}

bool SmPreviewRuntime::CompleteAnimatedTransition(const BrxSmRecord &machine)
{
    if (!snapshot_.waitingForAnimation || snapshot_.nextStateOid < 0)
        return false;
    snapshot_.currentStateOid = snapshot_.nextStateOid;
    Log("Entered state OID " + std::to_string(snapshot_.currentStateOid));
    FinishWithoutTransition();
    return ChooseTransition(machine);
}

bool SmPreviewRuntime::ChooseTransition(const BrxSmRecord &machine)
{
    // Mirror ChooseSmaTransition: reaching a goal immediately adopts that
    // state's automatic-next target, then instant edges may chain in one call.
    const std::size_t safetyLimit = machine.states.size() + machine.transitions.size() + 1;
    for (std::size_t safety = 0; safety < safetyLimit; ++safety)
    {
        const int currentIndex = StateIndex(machine, snapshot_.currentStateOid);
        if (currentIndex < 0)
        {
            FinishWithoutTransition();
            return false;
        }
        if (snapshot_.goalStateOid == snapshot_.currentStateOid)
            snapshot_.goalStateOid = machine.states[static_cast<std::size_t>(currentIndex)].automaticNextOid;
        if (snapshot_.goalStateOid < 0)
        {
            FinishWithoutTransition();
            Log("Stopped: state has no automatic-next goal");
            return true;
        }
        const int goalIndex = StateIndex(machine, snapshot_.goalStateOid);
        if (goalIndex < 0)
        {
            FinishWithoutTransition();
            Log("Stopped: goal OID is missing from the SM");
            return false;
        }

        // Distance-to-goal over forward and reversible edges is equivalent to
        // the retail breadth-first route marking and is easier to audit.
        std::vector<int> distance(machine.states.size(), -1);
        std::queue<int> pending;
        distance[static_cast<std::size_t>(goalIndex)] = 0;
        pending.push(goalIndex);
        while (!pending.empty())
        {
            const int reached = pending.front();
            pending.pop();
            for (const auto &transition : machine.transitions)
            {
                const int from = StateIndex(machine, transition.fromOid);
                const int to = StateIndex(machine, transition.toOid);
                if (from < 0 || to < 0)
                    continue;
                if (to == reached && distance[static_cast<std::size_t>(from)] < 0)
                {
                    distance[static_cast<std::size_t>(from)] = distance[static_cast<std::size_t>(reached)] + 1;
                    pending.push(from);
                }
                if ((transition.flags & 1) != 0 && from == reached && distance[static_cast<std::size_t>(to)] < 0)
                {
                    distance[static_cast<std::size_t>(to)] = distance[static_cast<std::size_t>(reached)] + 1;
                    pending.push(to);
                }
            }
        }
        if (distance[static_cast<std::size_t>(currentIndex)] < 0)
        {
            snapshot_.goalStateOid = -1;
            FinishWithoutTransition();
            Log("Stopped: goal is unreachable");
            return false;
        }

        struct Candidate
        {
            std::size_t index;
            bool reverse;
        };
        std::vector<Candidate> candidates;
        float totalWeight = 0.0f;
        for (std::size_t index = 0; index < machine.transitions.size(); ++index)
        {
            const auto &transition = machine.transitions[index];
            const bool forward = transition.fromOid == snapshot_.currentStateOid;
            const bool reverse = (transition.flags & 1) != 0 && transition.toOid == snapshot_.currentStateOid;
            if (!forward && !reverse)
                continue;
            const std::int16_t neighborOid = forward ? transition.toOid : transition.fromOid;
            const int neighbor = StateIndex(machine, neighborOid);
            if (neighbor < 0 ||
                distance[static_cast<std::size_t>(neighbor)] + 1 != distance[static_cast<std::size_t>(currentIndex)])
                continue;
            candidates.push_back({index, reverse && !forward});
            if (std::isfinite(transition.probability) && transition.probability > 0.0f)
                totalWeight += transition.probability;
        }
        if (candidates.empty())
        {
            snapshot_.goalStateOid = -1;
            FinishWithoutTransition();
            Log("Stopped: shortest route has no usable transition");
            return false;
        }
        Candidate chosen = candidates.back();
        if (totalWeight > 0.0f)
        {
            float choice = Random01() * totalWeight;
            for (const Candidate candidate : candidates)
            {
                choice -= (std::max)(machine.transitions[candidate.index].probability, 0.0f);
                if (choice < 0.0f)
                {
                    chosen = candidate;
                    break;
                }
            }
        }
        const auto &transition = machine.transitions[chosen.index];
        snapshot_.activeTransitionIndex = static_cast<std::int32_t>(chosen.index);
        snapshot_.reverse = chosen.reverse;
        snapshot_.nextStateOid = chosen.reverse ? transition.fromOid : transition.toOid;
        snapshot_.animationObjectIndex = transition.ownsAnimation ? transition.animationObjectIndex : -1;
        Log(std::string(chosen.reverse ? "Reverse " : "Forward ") + std::to_string(snapshot_.currentStateOid) + " -> " +
            std::to_string(snapshot_.nextStateOid));
        if (transition.ownsAnimation)
        {
            snapshot_.waitingForAnimation = true;
            return true;
        }
        snapshot_.currentStateOid = snapshot_.nextStateOid;
        Log("Entered state OID " + std::to_string(snapshot_.currentStateOid) + " instantly");
        FinishWithoutTransition();
        if (snapshot_.currentStateOid != snapshot_.goalStateOid)
            continue;
        // Loop once more so the reached state's automatic-next behavior is
        // handled exactly like ChooseSmaTransition's recursive call.
    }
    FinishWithoutTransition();
    Log("Stopped: instant/automatic transition cycle detected");
    return false;
}
