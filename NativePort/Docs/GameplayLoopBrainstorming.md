# Career Mode Design

## Table of Contents

- [Overview](#overview)
- [Core Goals](#core-goals)
- [Core Gameplay Loop](#core-gameplay-loop)
- [Contract Flow](#contract-flow)
  - [Contract Sources](#contract-sources)
  - [Contract Types](#contract-types)
  - [Contract Completion Conditions](#contract-completion-conditions)
  - [Contract Failure Conditions](#contract-failure-conditions)
- [Aircraft Flow](#aircraft-flow)
  - [Aircraft Selection](#aircraft-selection)
  - [Aircraft Initialization States](#aircraft-initialization-states)
  - [Aircraft Roles](#aircraft-roles)
- [Home Hangar](#home-hangar)
  - [Purpose](#purpose)
  - [Hangar Functions](#hangar-functions)
  - [Home Base Importance](#home-base-importance)
- [Destination Airstrips](#destination-airstrips)
  - [Remote Airstrip Functions](#remote-airstrip-functions)
  - [Airstrip Variability](#airstrip-variability)
- [Rewards and Progression](#rewards-and-progression)
  - [Reward Sources](#reward-sources)
  - [Reward Uses](#reward-uses)
  - [Progression Focus](#progression-focus)
- [Crash and Failure System](#crash-and-failure-system)
  - [Aircraft Damage Consequences](#aircraft-damage-consequences)
  - [Repair Timer](#repair-timer)
  - [Cargo Failure Rule](#cargo-failure-rule)
  - [Design Intent](#design-intent)
- [Mode Separation](#mode-separation)
  - [Sandbox / Creative Mode](#sandbox--creative-mode)
  - [Career Mode](#career-mode)
- [Design Philosophy](#design-philosophy)
- [Initial MVP Scope](#initial-mvp-scope)
- [Future Expansion](#future-expansion)
- [Summary](#summary)

---

## Overview

Career Mode shifts **TrueFlight** from a freeform sandbox into a structured progression-based game mode centered around aircraft ownership, contract work, risk management, and long-term fleet growth.

The player operates from a [home hangar](#home-hangar), selects aircraft and upgrades, accepts [contracts](#contract-flow), completes deliveries or drops, and returns to grow their operation through earned [rewards](#rewards-and-progression).

[Sandbox and Career Mode](#mode-separation) remain separate to preserve both unrestricted experimentation and curated progression.

[Back to top](#career-mode-design)

---

## Core Goals

- Provide a clear gameplay loop built around [contracts](#contract-flow) and [aircraft management](#aircraft-flow)
- Create meaningful progression through aircraft, upgrades, and earnings
- Introduce consequences for crashes and operational mistakes through the [crash and failure system](#crash-and-failure-system)
- Keep [Sandbox Mode](#sandbox--creative-mode) intact as the unrestricted experimental mode
- Restrict [Career Mode](#career-mode) to curated aircraft and progression-compatible content

[Back to top](#career-mode-design)

---

## Core Gameplay Loop

1. Player spawns at the [home hangar](#home-hangar)
2. Player selects an available [aircraft](#aircraft-selection)
3. Player configures:
   - parts
   - upgrades
   - loadout / cargo configuration
4. Player selects a [contract](#contract-flow)
5. Player is loaded into the home runway / airstrip
6. Aircraft state is initialized as either:
   - [cold start](#aircraft-initialization-states)
   - [pre-warmed / auto-started](#aircraft-initialization-states)
7. Player takes off and travels to the destination
8. Contract is completed by one of the following:
   - air-dropping cargo into a target zone
   - landing at a destination airstrip
9. At the destination, the player may:
   - refuel
   - repair
   - reconfigure aircraft
   - accept an optional [continuation contract](#contract-types)
10. Player eventually returns to the [home airstrip](#home-base-importance)
11. Player receives [rewards, payments, and progression updates](#rewards-and-progression)

[Back to top](#career-mode-design)

---

## Contract Flow

Contracts are the main structure connecting the [home hangar](#home-hangar), [destination airstrips](#destination-airstrips), [aircraft usage](#aircraft-flow), and [rewards](#rewards-and-progression).

### Contract Sources

Contracts are presented to the player while at an airfield or hangar.

See also:
- [Home Hangar](#home-hangar)
- [Destination Airstrips](#destination-airstrips)

### Contract Types

Initial supported contract styles:

- **Standard delivery**
  - transport cargo from one airstrip to another
- **Air-drop contract**
  - deliver cargo into a designated zone without landing
- **Continuation contract**
  - follow-up job accepted after completing a previous contract
  - may route the player further away or gradually back toward home

Related systems:
- [Core Gameplay Loop](#core-gameplay-loop)
- [Rewards and Progression](#rewards-and-progression)

### Contract Completion Conditions

A contract may require one of the following:

- successful landing at the correct destination
- successful delivery into a valid drop zone
- cargo delivered in acceptable condition
- aircraft arriving within required mission constraints

Related systems:
- [Contract Failure Conditions](#contract-failure-conditions)
- [Crash and Failure System](#crash-and-failure-system)

### Contract Failure Conditions

A contract fails if any required success condition is not met, including:

- aircraft destruction or crash resulting in loss of mission viability
- cargo loss or destruction
- invalid drop or incorrect landing destination
- forfeit due to crash while actively carrying cargo

See:
- [Cargo Failure Rule](#cargo-failure-rule)
- [Aircraft Damage Consequences](#aircraft-damage-consequences)

[Back to top](#career-mode-design)

---

## Aircraft Flow

Aircraft are central to [Career Mode](#career-mode), [contract completion](#contract-flow), and [progression](#rewards-and-progression).

### Aircraft Selection

Career Mode allows the player to choose from **owned and curated aircraft only**.

See:
- [Career Mode](#career-mode)
- [Aircraft Roles](#aircraft-roles)

### Aircraft Initialization States

Aircraft may begin a mission in one of two states:

- **Cold**
  - engine off
  - full startup process required
- **Pre-warmed / Auto-started**
  - aircraft is prepared for immediate departure

This can later be influenced by:
- difficulty mode
- accessibility settings
- aircraft type
- service state

Related sections:
- [Core Gameplay Loop](#core-gameplay-loop)
- [Design Philosophy](#design-philosophy)

### Aircraft Roles

Career aircraft should be authored with intentional gameplay roles, such as:

- short-field utility aircraft
- light cargo aircraft
- fast courier aircraft
- rugged remote-strip aircraft

Aircraft should differ in:
- range
- cargo capacity
- runway requirements
- maintenance cost
- handling difficulty
- upgrade compatibility

See also:
- [Destination Airstrips](#destination-airstrips)
- [Rewards and Progression](#rewards-and-progression)

[Back to top](#career-mode-design)

---

## Home Hangar

The [home hangar](#home-hangar) is the player’s primary long-term base and the main anchor point for [Career Mode](#career-mode).

### Purpose

The home hangar acts as the player’s primary long-term base of operations.

It connects directly to:
- [Aircraft Flow](#aircraft-flow)
- [Contract Flow](#contract-flow)
- [Rewards and Progression](#rewards-and-progression)

### Hangar Functions

The home hangar should support:

- aircraft selection
- upgrade installation
- part management
- long-term maintenance
- fleet overview
- contract selection
- progression display

Related systems:
- [Aircraft Selection](#aircraft-selection)
- [Reward Uses](#reward-uses)

### Home Base Importance

The home hangar is the player’s central recovery and progression hub. It should be the most reliable place for:

- full servicing
- storage
- repairs
- upgrade access
- major progression milestones

See also:
- [Repair Timer](#repair-timer)
- [Reward Uses](#reward-uses)

[Back to top](#career-mode-design)

---

## Destination Airstrips

Destination airstrips act as remote operational points rather than full replacements for the [home hangar](#home-hangar).

### Remote Airstrip Functions

A destination airstrip may allow limited:

- refueling
- repair
- aircraft turnaround
- continuation contract access

Related systems:
- [Contract Types](#contract-types)
- [Repair Timer](#repair-timer)

### Airstrip Variability

Airstrips should differ by:

- runway length
- runway surface
- repair capability
- service speed
- risk level
- accessibility
- environment / terrain constraints

This allows location to affect mission difficulty and [aircraft viability](#aircraft-roles).

See also:
- [Crash and Failure System](#crash-and-failure-system)
- [Design Philosophy](#design-philosophy)

[Back to top](#career-mode-design)

---

## Rewards and Progression

Rewards and progression give the [core gameplay loop](#core-gameplay-loop) long-term value.

### Reward Sources

The player earns rewards by:

- completing contracts
- successfully delivering cargo
- safely returning aircraft
- chaining continuation contracts
- maintaining operational efficiency

See:
- [Contract Flow](#contract-flow)
- [Home Base Importance](#home-base-importance)

### Reward Uses

Rewards and earnings can be used for:

- purchasing better aircraft
- buying upgrades
- replacing or repairing damaged aircraft
- expanding long-term capability

Related sections:
- [Aircraft Flow](#aircraft-flow)
- [Crash and Failure System](#crash-and-failure-system)

### Progression Focus

Progression should emphasize:

- expanding the player’s aircraft roster
- increasing mission range and difficulty
- unlocking more profitable contracts
- improving reliability and efficiency

See also:
- [Initial MVP Scope](#initial-mvp-scope)
- [Future Expansion](#future-expansion)

[Back to top](#career-mode-design)

---

## Crash and Failure System

This system creates consequences that feed back into [contract flow](#contract-flow), [aircraft management](#aircraft-flow), and [progression](#rewards-and-progression).

### Aircraft Damage Consequences

If the player crashes, the aircraft becomes temporarily **unusable**.

See also:
- [Repair Timer](#repair-timer)
- [Cargo Failure Rule](#cargo-failure-rule)

### Repair Timer

A repair timer is applied based on:

- **damage severity**
- **capability of the current airstrip**

Expected repair range:

- **minimum:** 30 seconds
- **maximum:** 45 minutes

Related systems:
- [Destination Airstrips](#destination-airstrips)
- [Home Base Importance](#home-base-importance)

### Cargo Failure Rule

If a crash occurs while a cargo package is being carried:

- the active contract is **forfeited**

See:
- [Contract Failure Conditions](#contract-failure-conditions)

### Design Intent

Crashes should carry meaningful operational consequences without fully ending player progression.

This system encourages:
- safer flying
- aircraft care
- route planning
- fleet management

Related sections:
- [Design Philosophy](#design-philosophy)
- [Rewards and Progression](#rewards-and-progression)

[Back to top](#career-mode-design)

---

## Mode Separation

Career progression depends on a hard separation between [Sandbox / Creative Mode](#sandbox--creative-mode) and [Career Mode](#career-mode).

### Sandbox / Creative Mode

Sandbox Mode remains the current unrestricted experimental mode.

#### Sandbox Features

- freely loaded external models
- unrestricted experimentation
- freeform spawning and usage
- no strict progression requirements

See also:
- [Career Mode](#career-mode)

### Career Mode

Career Mode is a curated progression mode with authored content and controlled balance.

#### Career Mode Restrictions

- freely loaded models are disabled
- only curated aircraft are available
- aircraft content is hand-authored for consistency

#### Reason for Restriction

Curated aircraft allow:

- correct control surface alignment
- reliable tuning and balance
- clean upgrade pathing
- predictable progression
- better gameplay consistency

Related systems:
- [Aircraft Roles](#aircraft-roles)
- [Rewards and Progression](#rewards-and-progression)

[Back to top](#career-mode-design)

---

## Design Philosophy

Career Mode should feel closer to **SnowRunner with aircraft** than to a pure free-flight sandbox.

The intended experience is based on:

- selecting the right aircraft for the job
- handling operational risk
- flying between meaningful locations
- managing downtime and damage
- building a capable fleet over time

The [hangar](#home-hangar) replaces the garage, and aircraft logistics replace ground vehicle hauling.

This philosophy informs:
- [Contract Flow](#contract-flow)
- [Aircraft Flow](#aircraft-flow)
- [Crash and Failure System](#crash-and-failure-system)
- [Rewards and Progression](#rewards-and-progression)

[Back to top](#career-mode-design)

---

## Initial MVP Scope

Recommended first implementation scope:

- 1 home hangar
- 2 to 3 destination airstrips
- 2 aircraft
- 3 contract types
- basic continuation contracts
- basic aircraft damage persistence
- repair timer system
- payment and reward flow
- simple upgrade path
- home return loop

This should be enough to prove the mode is fun before expanding content.

See also:
- [Core Gameplay Loop](#core-gameplay-loop)
- [Future Expansion](#future-expansion)

[Back to top](#career-mode-design)

---

## Future Expansion

Potential future extensions:

- more contract categories
- aircraft classes and licensing tiers
- weather-affected missions
- long-distance route chains
- multiple owned aircraft in service
- premium repair options
- deeper maintenance simulation
- region-based progression
- reputation systems
- advanced difficulty presets

See:
- [Initial MVP Scope](#initial-mvp-scope)
- [Design Philosophy](#design-philosophy)

[Back to top](#career-mode-design)

---

## Summary

Career Mode should transform TrueFlight into a structured game loop built around:

- [home hangar management](#home-hangar)
- [curated aircraft progression](#aircraft-flow)
- [contract-based flying](#contract-flow)
- [destination-based operations](#destination-airstrips)
- [crash consequences](#crash-and-failure-system)
- [long-term rewards and upgrades](#rewards-and-progression)

[Sandbox Mode](#sandbox--creative-mode) remains the unrestricted experimental space, while [Career Mode](#career-mode) becomes the balanced, authored, progression-focused game mode.

[Back to top](#career-mode-design)