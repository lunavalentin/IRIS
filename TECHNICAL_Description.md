# IRIS V4: A Near-Real-Time Autonomous Spatial Convolution Framework

## 1. Introduction
The IRIS V4 plugin represents a shift in how dynamic spatial audio reproduction is handled across multi-DAW instances. Moving beyond static "A/B" impulse response comparisons, IRIS implements a physics-driven continuous interpolation layer constructed atop nearest-neighbor acoustic networks. 

This paper breaks down the core feature components of IRIS V4: the Gaussian-weighted crossfading engine, the 2D ray-cast occlusion wall system, the momentum-based listener physics layer, and the decentralized symmetric multi-listener tracking system.

## 2. Core Convolution Engine & Gaussian Neighbors 

The acoustic footprint of IRIS relies heavily on real-time continuous convolution calculation as a listener navigates a 2D bounding layout of physical sound characteristics (an impulse response point-cloud).

### 2.1 The Nearest Neighbor Blending Function
Rather than switching definitively when a listener $L (x_L, y_L)$ crosses a hard boundary between two IR points $P_i (x_{p_i}, y_{p_i})$ and $P_j$, the engine calculates a dynamic Gaussian influence weight $w_i$ over distance $d_i$:

$$ d_i^2 = (x_{p_i} - x_{L})^2 + (y_{p_i} - y_{L})^2 $$

Using a parameterized spatial spread coefficient ($\sigma$), the raw Gaussian weight is structured as:
$$ w_i = e^{\frac{-d_i^2}{2 \sigma^2}} $$
where the spread $\sigma = 0.05 + 1.5 \times s^2$ maps an arbitrary user UI variable $s \in [0, 1]$.

These weights are gathered uniformly across all points, and the active contributions of the Top-N neighbors are dynamically selected, sum-normalized in linear time ($O(N)$), and subsequently passed into a one-pole smoothing filter to alleviate zippering artifacts when the listener teleports.

## 3. The 2D Sub-Graph Occlusion System

A critical aspect of indoor acoustical reproduction is the ability to attenuate direct sound along non-line-of-sight (NLOS) intersections. IRIS accommodates this through an implementation of dynamic line-segment geometry tests over continuous acoustic space.

### 3.1 Ray-Segment Intersection
Whenever an active $P_i$ intersects with an arbitrarily drawn Wall $W_k$ represented by bounds $[(x_{1, k}, y_{1, k}), (x_{2, k}, y_{2, k})]$ from the origin of $L$, the plugin attenuates $P_i$'s effective gain weight $w_i$.

For collinear ray-casts, intersection logic uses standard parameter $t$ evaluation mapping:
$$ W_{att}(i) = 1.0 - (\text{Wall}_A \times (1.0 - L_{opacity})) $$
Where $W_{att}$ clamps the resultant linear crossfade down by the attenuation constant $A$. Thus, sliding across an intersecting wall boundary casts a gradient audio shadow across the soundscape.

## 4. Momentum and the Inertia Matrix

The user's direct interaction is intentionally decoupled from the computational engine through the integration of an elementary physics envelope, known as Inertia interpolation. 

### 4.1 Inertial Equations (Euler Integration)
To smooth non-linear, instantaneous user actions (like mouse jumping or MIDI macro execution), the $L$ coordinate $x_L$ acts as an instantaneous "target" property, while $currentX$ operates as the mathematical reality. The differential updates per time-step (60 Hz clock) using coefficient $\alpha$:

$$ \alpha = 1.0 - 0.98 \times I_{\text{user}} $$
$$ currentX_t = currentX_{t-1} + (x_L - currentX_{t-1}) \times \alpha $$

With the addition of a Global `Freeze` parameter, the user can instantly decouple $currentX$ from $x_L$, resulting in the physical retention of the engine state while the UI visual boundary can be continuously analyzed.

## 5. Symmetric Edge Graphs and OSC Sync

A crucial deployment requirement for large-scale sonic installations is the requirement to run autonomous, synchronized convolutions simultaneously across distinct DAWs over a shared IP network.

### 5.1 Symmetric Network Propagtion
Instead of typical master/slave routing logic, IRIS implements symmetric listener links. When $L_A$ is declared linked to $L_B$, a bidirectional graph edge $(L_A, L_B)$ is inserted to the global network graph, and synchronized across every running VST via OSC over `127.0.0.1:9002`.

When any $L_k$ coordinate undergoes a displacement operation locally:
1. The originating VST locks its state mechanism.
2. A local recursive Breadth-First-Search (BFS) operates across the Adjacency List.
3. Every connected sibling node displacement $\Delta L$ matches the parent.
4. An immediate snapshot is transmitted asynchronously via OSC to replicate target $x, y$ coordinates into the remote bounds of the parallel clients. 

### 5.2 Global Parameter Mirroring
Every global engine feature sets flags via the `AudioProcessorValueTreeState::Listener` class. Upon mutation, global values (like Mix, Spread, Freeze, and Inertia) construct local OSC signatures and replicate perfectly.

## 6. Conclusion
IRIS V4 represents an extensive step in architectural robustness and feature execution, decoupling direct positional rendering into a mathematical crossfading layer governed by graph algorithms and spatial physics.
