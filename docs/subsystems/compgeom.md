# Computational geometry: nGZCompGeom and nGZNumerics (subsystem notes)

The geometry and numerics library the scenegraph calls: linear forwarders, frames and rigid transforms, the convex shapes
(spheres, cones, cuboids), cBoundT<cITriangleSetShape> with its AABB tree, cFrustumShape, the 4x4 matrix helpers and
nGZNumerics::cPerlinNoise. Split out of `docs/subsystems/scenegraph.md` by lane VL (2026-11-03); the scenegraph
campaign planned it as G0 and G0b (lanes NY, PC-PV, TR, TS). Source in `src/compgeom/`, tests `tests/compgeom_*.cpp`.
Float pitfalls that apply here (NaN payloads, x87 precision) are in scenegraph.md's "Pitfalls and risks". Superseded
plans are in `docs/archive/scenegraph-lane-history.md`.

Note on the status below: 0x19be70, listed as left in the G0b sections, was done by lane TR ("Lane TR" below).

## Prerequisites (lane NX)

- **nGZCompGeom / nGZNumerics** (~11 KB, filed under rendering-gfx; lane G0 below): cBoundT<cITriangleSetShape> 4.3,
  cConvexT<cITranslatedSphereShape> 1.9, cLinear3T<cISegment3> 1.0, cConvexT<cIOrientedConeShape> 0.8,
  cLinear3T<cILine3> 0.7, cPerlinNoise 0.7, cConvexT<cIOrientedCuboidShape> 0.6, cLinear3T<cIRay3> 0.4, cRay3, cSegment3.
  Bounds, octree and culling call them; their director's factories start at 0x18ead0.

## G0: the linear forwarders (lane NY)

- G0, linear family (lane NY): nGZCompGeom cLinear3T<cILine3 / cIRay3 / cISegment3>, cRay3, cSegment3, their three
  factories (0x18ead0 / 0x18eb00 / 0x18eb30), the containment tests (Line3 0x18ec30, the ray's 0x1977d0, Segment3
  0x18ee90): 113 verified (~4.6 KB), `src/compgeom/{CompGeom.h,Linear3.h,Linear3.cpp}`, group CompGeomLinear3,
  tests/compgeom_linear.cpp (4 tests, 31 mutants killed). Whole classes in the standalone shape: our vtables, the
  factories `return new X`, the slot and lifetime functions hooked to adapters except the folded AddRef 0x18e960 and the
  shape-data getter 0x18ea60 (slots 1, 34, 35: shared with other classes, ours in our vtables only). Original calls left: the shape-pair
  helpers (G0b) and three transform functions (0x19c250, 0x19c3b0, 0x19c770).

## G0b results (lanes PC-PV)

- G0b part 1 (lane PC, 2026-10-28): **cBoundT<cITriangleSetShape>** (the triangle set shape) with its AABB tree, and
  the **frame members** Linear3 called as originals.
  - `src/compgeom/TriangleSet.{h,cpp}`, `src/compgeom/Boxes.cpp` (group CompGeomTriangleSet): the set (vertices,
    16-bit corner indices, the tree) in std::vector shapes whose memory comes from the allocator service as the
    game's does; the tree builder (top-down split at the box centre along the longest side) and refit; the shape class
    with our vtables. The director's factory 0x196560 is the switch (hooked); the set's members Read uses (0x195af0,
    0x196d00, 0x196cc0, 0x196300, 0x1970f0) and BoundBox::Grow 0x19d430 / Cuboid::BoxAt 0x1902e0 are hooked; the slot
    bodies, both destructors and 23 tree / builder / vector helpers are in effect through callers. cGeometryDataContainer
    Read now calls ours.
  - `src/compgeom/Frames.{h,cpp}` (group CompGeomFrames): VectorTo 0x19c770, InverseVectorTo 0x19c250,
    InversePointTo 0x19c3b0, InverseMatrix 0x19cb30, Scale 0x19cf10, TransposeMatrix 0x1a6590, hooked; Linear3's
    frame functions call them.
  - Tests: tests/compgeom_triangle_set.cpp (Data: random resize / vertex / triangle / build / refit sequences with an
    allocation log; Slots: random slot calls on factory objects and clones, stand-ins for the 14 intersection helpers
    still original; Boxes), tests/compgeom_frames.cpp (every flag branch, in place and apart, up to two NaNs).
  - **Original calls left:** the intersection helpers 0x19ada0-0x19b290 (lines, rays, segments against the set),
    0x19f8e0 (set against cuboid), 0x1a01c0 (set against set), the cuboid boxes 0x18ff90 / 0x190170, the rigid
    transform's point 0x19c900, the quaternion-to-frame 0x19d0d0, the general 3x3 inverse 0x1a62e0 (all done by PH, next).

- G0b part 2 (lane PH, 2026-10-29): **rigid transform, quaternion helpers, LU inverse, cuboid boxes** (16 functions).
  - `src/compgeom/Rigid.{h,cpp}` (group CompGeomRigid), `Frames.{h,cpp}` (LuDecomposition3, InverseMatrix3), members
    declared in CompGeom.h: RigidTransform3 TransformPoint 0x19c900, InverseTransformPoint 0x19c6d0,
    InverseTransformVector 0x19c390, ToMatrix 0x19c230, Compose 0x19c080; FrameMath::SetRotation 0x19d0d0; QuaternionToMatrix
    0x1a77f0; RotateByInverseQuaternion 0x1a6ad0; Dot87 0x18fbf0 (x87 dot product); BoundBox Corner 0x19d390 /
    Transform 0x19d730; Cuboid BoxInFrame 0x18ff90 / BoxInRigid 0x190170; InverseMatrix3 0x1a62e0 with Decompose 0x1a7aa0
    / Solve 0x1a7e50. FrameMath::InverseMatrix 0x19cb30 no longer calls an original.
  - Callers switched to ours: TriangleSet, cLooseOctreePartitionNode, cTransformNodeImpl, the audio demon and EA renderer
    size files, cTSSGSystemFrame.
  - Test: tests/compgeom_rigid.cpp (five tests; random plus injected NaN pairs, bit for bit). The x87 dot product has no
    operand order: of two NaNs the larger significand wins (equal: the positive one), a signalling memory operand loses
    to a NaN already in the register (all found by the differential test).
  - **Also changed:** math::Quaternion::PreMultiplyNormalised (src/misc/Quaternion.cpp, another lane's) now keeps the
    original's NaN operand order (Compose needed it); simgeom::RotateByQuaternion 0x1a6c40 is still not NaN-order exact
    (Rigid.cpp has a local ordered copy for TransformPoint).
  - **x87 precision** (lane PJ): Dot87 reads the calling thread's x87 precision control and rounds each product and sum
    to 24 bits when it is 24-bit (D3D9 sets that without FPU_PRESERVE; src/misc/X87.h). The Dot87 and box tests run
    at 53-bit and again at 24-bit. Which precision the game's threads have is the host probe's to confirm
    (TS2HOST_X87PROBE=1).
  - **Original calls left:** the compositions 0x19be70 (frame with rigid transform, 513 bytes) and 0x19be20 (frame with
    frame); the intersection helpers of part 1.

- G0b part 3 (lane PI, 2026-10-29): **line / ray / segment tests against cone, sphere and cuboid** (57 functions).
  - `src/compgeom/ShapePairs.{h,cpp}` (group CompGeomPairs): the cores (cone 0x1978a0 / 0x197b00, sphere 0x198cf0 /
    0x198e50, cuboid 0x1999e0 / 0x199b90, stdcall ret 0x10), the test and first-hit wrappers (templates TestPair /
    FirstHitPair over shape, placement Oriented / Translated / InFrame, and Line / Ray / Segment) and the three
    point-to-line / ray / segment squared distances 0x1a2950 / 0x1a2a80 / 0x1a2be0. The sphere tests are
    `r*r >= distance` from the centre; the sphere hits use the solver. All 48 stubs in Linear3.cpp now call ours.
  - Test: tests/compgeom_pairs.cpp (six tests, random shapes, NaN pairs, lattice boundary cases; t and return value bit
    for bit). 24 mutants: 19 killed, 5 judged equivalent (sum order of squares that are equal, X87Pick argument order,
    sphere `disc >= 0` branch).
  - Quirk kept: the cuboid solver returns false with t0/t1 unwritten when every axis is parallel and inside.
  - **Unchecked in game:** Dot87 / X87Pick assume 53-bit x87 precision control.
  - **Original calls left:** the separating axis tests (the triangle-set walkers are lane PK's, next entry).
- G0b part 4 (lane PK, 2026-10-29): **triangle-set walkers** (22 functions, `src/compgeom/TrianglePairs.{h,cpp}`).
  - Triangle cores 0x1a3240 (Moller-Trumbore, cdecl `t, e1, e2, s, dir`; `t` written only on true; parallel when
    `0x2f03126f >= |det|`; the divide is an x87 `fdiv` with a memory divisor, so Rounded at 24-bit) and its wrappers
    0x1a3130 / 0x1a2db0 / 0x1a2ec0 / 0x1a2fe0; the box span 0x19ace0 (returns 2 or 0); the test walks 0x19b670 / 0x19b8e0 /
    0x19bb50, the hit walk 0x19b330, the forwarders 0x19ada0-0x19ade0, 0x19afe0-0x19b0b0 and the in-frame wrappers
    0x19ae00-0x19af40, 0x19b150-0x19b290. The six Linear3.cpp stubs and the twelve TriangleSet.cpp calls now call ours.
  - Test: tests/compgeom_triangle_pairs.cpp (CompGeomTrianglePairs_Cores / _Walkers: 1.4M cases per precision; trees
    over raw set blocks with every child pairing, exact and perturbed boxes, walkers started from random nodes, lattice
    cases, frames of every flag kind). 46 mutants: 42 killed, 4 equivalent.
  - Quirks kept: in the hit walk a node with two child nodes returns the OR of the two walks only (the node's own hits
    cannot exist there anyway), a tie in entry time walks the right box first, and a node with a left node and a right
    triangle takes the right triangle first in the test walk (unobservable: pure OR).
  - tests/compgeom_triangle_set.cpp: HelperPatches also patches `h->replacement` of the helper RVAs (as
    compgeom_linear.cpp does), since the slots now call ours.
- G0b part 5 (lane PL, 2026-10-29): **separating-axis tests of triangle sets** (14 functions,
  `src/compgeom/SeparatingAxes.{h,cpp}`): the set against a cuboid 0x19f8e0 (walk 0x19f930, triangle against box
  0x1a3410) and against another set 0x1a01c0 (walk 0x1a0210, triangle against triangle 0x1a4620 with the coplanar case
  0x1a42d0 and point-in-triangle 0x1a4ea0), the context builder 0x19ffd0 (0xb8 bytes: frame A * inverse(B), its inverse,
  |matrix| + FLT_EPSILON for both), the frame members 0x19cca0 (Invert) and 0x19be20 (Compose), the 3x3 product
  0x1a5d20 / 0x1a6650 and the box span forwarder 0x19f800. TriangleSet.cpp's two remaining original calls (0x19f8e0,
  0x1a01c0) now call ours; no original call is left in nGZCompGeom's triangle sets.
  - Test: tests/compgeom_separating.cpp (seven tests; frames and products with two-NaN cases to check operand order, the
    triangle-box test on lattice, corner-on-box and large-triangle plane cases, trees over raw set blocks walked from
    random nodes with a context the original builds, triangle pairs in lattice, coplanar, near-coplanar (around the
    1.19e-10 snap), shared-corner and junk kinds). 74 mutants: 71 killed, 3 equivalent (the p6/p3 swap differs only for
    NaN or infinite input, `nz >= 0` only for a zero normal, the plane offset's sign because the box test is symmetric
    about zero).
  - Quirks kept: 0x1a0210 returns in al only (callers read al); every test is an ordered comparison, so a NaN fails the
    `ja`-style tests and passes the `jbe`-style ones exactly where the original's comiss does (0x1a3410's axis tests take
    the second arm on NaN); the plane distance snap `0x2f03126f >= |d|` keeps a NaN.
  - **Original calls that can now switch to ours (other lanes' files):** cTransformNodeImpl's 0x19be20
    (`FramePlacement::Compose`) and cLooseOctreePartitionNode's 0x19f800 (`TreeBoxLineSpan`).

- G0b part 6 (lane PN, 2026-10-29): **nGZNumerics::cPerlinNoise** as a whole class (`src/numerics/PerlinNoise.{h,cpp}`,
  `tests/numerics_perlin_noise.cpp`, status group PerlinNoise; the director's factory 0x98c450 is the switch).
  - **Map.** Object 0x1924 bytes: +0 vptr (vtable 0xe5d0dc, one slot: the deleting destructor 0x98c3b0), +4 vbptr
    (vbtable 0xe5d130), +8 reference count (0 at first), +0xc dimensions, +0x10 gradient1[256] floats, +0x410
    gradient2[256][2] (cos, sin), +0xc10 gradient3[256][3] (unit vectors), +0x1810 permutation[256] bytes, +0x1910
    vtordisp, +0x1914 the cIGZUnknown face (vtable 0xe5d0e4: QueryInterface 0x98d330, AddRef 0x98c3e0, Release 0x98d3ab,
    the last Release deletes), +0x1918 vtordisp, +0x191c the cIPerlinNoise face (vtable 0xe5d0f4, 15 slots), +0x1920 its
    vbptr (vbtable 0xe5d13c). Plain `public virtual` bases in our compiler give the same layout (the test compares the
    offsets, vbtables and vtordisps with the game's object). Interface id 0xbd33210e. The factory returns the
    **cIGZUnknown** face (+0x1914), not the noise face; the director asks it for the interface.
  - Slots (cIPerlinNoise face): 0 Init(dims, seed) 0x98c4a0, 1 IsValidDimension(d) 0x98c6b0 (`d <= dims`), 2 / 4 / 6
    Noise1Cell / 2Cell / 3Cell (0x98c6d0, 0x98c7a0, 0x98c980: integer cell + fractions), 3 / 5 / 7 Noise1 / 2 / 3
    (0x98c740, 0x98c8e0, 0x98cd30: floor split then the cell), 8..13 the fractal forms (0x98ce00, 0x98cec0, 0x98cf40,
    0x98d050, 0x98d100, 0x98d260: sum of `cell * amplitude` over octaves, doubling the coordinates and multiplying
    amplitude by gain each octave; they call the cell slots through the same face), 14 OctaveCount (wrapper 0x98c400 over
    the static helper 0x98d3f0: the octaves that bring the amplitude under a minimum or fit a scale, the smaller, at
    least 1, rounded; reads 1/ln 2 from 0x10f4fbc, a float set by the static initialiser 0x13e3a0).
  - Init: a cRZRandom (ours, `src/rz/cRZRandom.h`) constructed with -1 (clock seed) then seeded with the argument;
    per entry: gradient1 = 1 - 2u, an angle u * 2pi (the product is an x87 fmul stored as a double) for gradient2,
    and for 3D a rejection-sampled point in the unit ball normalised; then a Fisher-Yates style shuffle of the
    permutation with `RandomUint32Uniform(256)`. The generator is `x * 663608941` mod 2^32: a seed that is a power of two
    times an odd number reaches zero (clock seed) or a constant, so tests avoid 0 and 0x80000000.
  - Quirks kept: the smoothing weight is `(3 - 2t) * (t * t)`; the 3D dot is `fy*gy + fx*gx + fz*gz`, the 2D dot
    `fx*gx + fy*gy`; the fractal sum is x87 (`fmul` by the amplitude then `fadd` of the running sum, stored as a float),
    so two NaNs resolve by the x87 rule (larger significand, the positive one when equal), not SSE's first operand;
    the octave coordinate doubling carries 1 into the integer part when `2f >= 1` (a NaN carries 1 and subtracts
    nothing); the octave count converts with the low word of a 64-bit truncation (`_ftol3`).
  - Test and result: tests/numerics_perlin_noise.cpp, 6 tests through the object's own vtables (layout, Init tables byte for byte, noise, fractal, octave count, QueryInterface/AddRef/Release); pass at 53-bit and 24-bit x87. 21 mutants: 20 killed, 1 equivalent (swapping the two operands of the first add in the 3D dot). The first run left the fractal NaN pick (larger significand wins) untested; the amplitude now takes payload and signed NaNs.
  - Not done: nothing in the class; callers (the roof manager 0x3130d0, the scene DB animation system 0x9db240,
    0xaec6b0) reach it through the interface and need no change.

## G0b, what is left (plan as of lane PV)

**G0b, what is left (plan, lane PC).** In dependency order; each step is a lane-sized piece.
1. *Transform functions 0x19bdc0-0x19cf80*: done by PH, and 0x19be20 by PL; left: 0x19be70 (small; any lane).
2. *Cuboid boxes* 0x18ff90 / 0x190170: done by PH.
3. *Shape-pair helpers 0x197d70-0x19b330*: cone, sphere and cuboid done by PI, the triangle-set walkers and cores by PK
   (see Done); the separating axis tests 0x19f8e0 / 0x1a01c0 with 0x19ffd0, 0x19f930 and 0x1a0210 by PL (G0b part 5).
   All of step 3 is done.
4. *cConvexT shapes* (translated / oriented sphere, cone, cuboid): each a cBoundT-style class with its director's
   factory as the switch, in the shape of TriangleSet.cpp (slots forwarding to the pair helpers of step 3).
   Lane PM, 2026-10-29. Five classes exist (no oriented sphere). Each has a bound class (cConvexT, made by Clone) and
   the factory's concrete class; slots 5-42 are the CONVEX_SLOTS list in `src/compgeom/ConvexShapes.h`.
   **Done: translated sphere** (id 0x7ce57a3c, factory 0x192460; `ConvexSphere.cpp`, `ConvexHelpers.cpp`, 39 verified,
   tests/compgeom_convex_sphere.cpp, three-sided through the vtable).
   **Done (lane PP, 2026-10-29): translated cuboid** (id 0x3ce553fa, factory 0x190450, bound vtable 0xda1584, concrete 0xda1640) and
   **oriented cuboid** (id 0x7ce6d2e1, factory 0x190420, vtables 0xda16fc / 0xda17b8): src/compgeom/ConvexCuboid.cpp (the
   slots), ConvexBoxes.cpp (the helpers 0x19cf80 AxisScales, 0x191ea0 SupportCorner, 0x19e070 BoxesMeetOnAxes15, 0x19dcb0
   CuboidsOverlap) and ConvexInternal.h (Placed, BoxGap and the sphere-reach test, shared with the sphere), 73 verified
   entries, tests/compgeom_convex_cuboid.cpp (three-sided Slots tests for each, Helpers test with NaNs at 53 and 24 bit).
   63 mutants over the cuboid files: all killed but two equivalent (&mData.frame versus its first member). The NaN pair
   test found a real order difference in the translated support point (x adds corner first, y and z position first); fixed.
   Oriented slot 12 (0x190c60) has its own body and is hooked; translated slot 12 is the shared 0x1474b0 (not hooked).
   Left, with what is known:
   **Done (lane PR, 2026-10-29): translated cone** (id 0xbd18b3f9, factory 0x193a80, bound vtable 0xda1a44, concrete 0xda1b00,
   0x1c bytes: data {radius, half height, Vec3 position} at +4, count +0x18): src/compgeom/ConvexCone.cpp (the slots and
   Create), ConvexHelpers.cpp (Cone::BoxInFrame 0x193630, BoxInRigid 0x1936b0, BoxAt 0x193730, SupportAlong 0x194450,
   ConvexBox::TransformRigid 0x19da20), 45 verified entries (29 slots with Create, 5 helpers, 12 dead wrappers),
   tests/compgeom_convex_cone.cpp (three-sided Slots test, Helpers test with apex-flip and kTiny probes, 53 and 24 bit,
   same-axis NaN pairs with different payloads). 49 mutants: all killed but seven equivalent (v.z >= 0 at zz = 0 never
   taken; operand order inside q, which only feeds a comparison; x*x versus y*y in s, its payload hidden by the NaN
   components; third product order and the Dot87 argument order in TransformRigid, where an earlier NaN term wins;
   +0.0 on a half extent). Two survivors (SupportPoint position-sum order) exposed a test gap, fixed by planting
   same-axis NaN pairs. Facts: slots 5, 6/7/9, 8, 11, 16, 17/18/19 are always false (6/7/9 = 0x193fb0 and 17/18/19 =
   0x193f40 folded, hooked once); slot 10 hands (cone, unit frame at the position) to the other bound's IntersectsConeAt,
   15 hands (frame, cone, unit frame at position) to IntersectsConeInAt, 20 hands (other frame, cone, position placed in
   the frame). SupportAlong: r2 = r*r, s = vx^2 + vy^2; for vz > 0 the apex (0,0,h) when vz^2 > ((vz^2+s)*r2)/(H*H+r2)
   with H = float(x87 h+h); |s| <= kTiny (bits 0x1e8637be) gives (0,0,-h); else k = float(sqrt(double(r2/s))) and
   (vx*k, vy*k, -h). TransformRigid: centre by rotate-and-translate, reach = float(Dot87(half, |row|)).
   **Done (lane PS, 2026-10-29): oriented cone** (id 0x7d18b3eb, factory 0x193a50, bound vtable 0xda1bbc, concrete 0xda1c78,
   0x2c bytes: data {radius, half height, RigidTransform3 translation + quaternion} at +4, count +0x28): the slots and Create
   in src/compgeom/ConvexCone.cpp (OrientedCone and the classes in ConvexShapes.h), no new helpers (BoxInFrame, BoxInRigid,
   SupportAlong, TransformRigid and the cone pair tests are the translated cone's), 41 verified entries (29 slots with Create,
   12 dead wrappers 0x19e660-0x19f090), tests/compgeom_convex_oriented_cone.cpp (three-sided Slots test, 53 and 24 bit,
   same-axis NaN pairs of direction, translation and quaternion). 42 mutants: all killed. Facts: slots 5, 6/7/9, 8, 11,
   12/13/14, 16, 17/18/19 always false (folded bodies 0x193b90, 0x193e00, 0x193ae0 hooked once); 10 hands (cone, own frame)
   to the other bound's IntersectsConeAt (the frame argument is ignored), 15 and 20 to IntersectsConeInAt (own frame, or own
   frame followed by the carrying frame); the linear slots and boxes use the own frame followed by the carrying frame
   ("In") or the rigid transform directly. 0x194400 and 0x193ad0 stay original (under 5 bytes).
   Left, with what is known:
   - (Oriented cone: done by lane PS, see above.) cFrustumShape (step 5) is done by lane PV; remaining shape work is 0x19be70.
   - Dead wrappers: done, 60 entries (sphere 12, translated cuboid 12, oriented cuboid 12, translated cone 12, oriented cone 12), "in_effect":"callers", traps on the
     hooks callers.py --vtable-only names (the sphere's, translated cuboid's and translated cone's slot hooks; the oriented cuboid's Create).
   - Sphere test gap closed (lane PP): kTiny >= |length| in Sphere::SupportAlong survived the > mutant; the Helpers test now probes
     the lengths at and 3 ulp either side of 1.1920929e-10 on each axis and sign; the mutant is killed.
   - Pattern from the sphere: a `Do<Slot>` member per shape-specific slot, hooked at its RVA too (SGLightShapes.h
     embeds game-vtable shapes no factory creates); `Placed(frame,p) = FrameAt(frame, FramePoint(frame,p))`; SSE ops keep
     the first operand's NaN, x87 ones the larger significand (test at 53 and 24 bit, run x87scan.py).
   - 0x19be70 (frame composition with a rigid transform) is not needed by the sphere; still a loose end.
5. *cFrustumShape*: **done (lane PV, 2026-10-29).** Not a pair-test shape: a cIShape only (id 0x0a6e6298, factory 0x197740,
   bound vtable 0xda1ef4, concrete 0xda1f18, 0x68 bytes: six planes of {a, b, c, d} at +4, count at +0x64). Its eight slots
   are QueryInterface (ids 1, 0x0a6e6298, cIShape 0x1ce54fd0; null out refuses), AddRef (0x1972b0, folded with cPortal and the
   thumbnail factory), Release, Clone (a bound copy; a refused id leaks it, ORIGINAL-BUG), GetClassID, the two data accessors
   (0x18ea60, 4 bytes, stays original) and the deleting destructor. src/compgeom/Frustum.{h,cpp}: the shape classes and the
   data functions the scene graph's cull code calls, hooked at their RVAs because original callers remain (Cull 0xa53500 is
   replaced; cDisplayList, cOverlayNode, cTSParticlesRenderable, FUN_a555d0/a61bb0/ae09e0 and cLooseOctreePartitionNode
   still call them): FromView 0x1975a0 (planes 2, 3, 4, 5, 0, 1 stored negated by a sign-bit flip), ClassifyBoxAt 0x197520,
   ClassifyBoxInFrame 0x1974a0, ClassifyBox 0x197330, and the two cdecl plane helpers PlaneBoxSide 0x1a28c0 and
   PlaneFrameBoxSide 0x1a2810 (reach = |a hx| + |b hy| + |c hz| in the order y, x, z; side = sign(r - d) - sign(d + r); the frame
   form turns the normal through ConvexFrame::TransposedVectorTo and takes the frame's offset as the centre). The classify
   loop: skip planes whose mask bit is set, return 1 at the first plane wholly outside (mask unchanged), else OR the planes
   wholly inside into the mask; result 0, or -1 when the mask is 0x3f. tests/compgeom_frustum.cpp: a three-sided Slots test
   and a Cull test (original against the replacements, 53 and 24 bit, same-axis NaN pairs, flip-point bisection).
   Loose end: src/sg/cLooseOctreePartitionNode.cpp:100 and :107 still call 0x1975a0 / 0x197520 through OriginalFunction
   wrappers (not this lane's file); the director table literal in src/rz/COMDirectorData.cpp:49 still names 0x197740.
6. *nGZNumerics::cPerlinNoise* (vtables 0xe5d0dc / 0xe5d0e4 / 0xe5d0f4): done by PN (G0b part 6).

## Lane TR: the 4x4 math helpers (src/compgeom/Matrix4.*)

- Ours now, hooked at their RVAs (db/status/CompGeomMatrix4.jsonl, tests/compgeom_matrix4.cpp): 0x1a5eb0 Matrix4Multiply
  (out[4i+j] = sum_k a[4k+j] * b[4i+k], operand order per term from a table), 0x1a6410 Matrix4Inverse (LU, 16 zeros when the
  determinant is 0; NaN determinants proceed), 0x1a5870 Matrix4Translation, 0x1a7200 Matrix4FromRotationScaleTranslation
  (args rotation, scale, translation, out; the old wrapper had scale and translation swapped in name only), 0x19c0e0
  FrameMath::ToMatrix4, 0x19c1c0 RigidTransform3::ToMatrix4, 0x19c800 RigidTransform3::RotateVector, 0x19be70
  FramePlacement::ComposeRigid, and the LU object 0x1a7c50 Decompose / 0x1a8030 Solve (0x54 bytes: lu[16], mRow[4] at +0x40,
  determinant at +0x50).
- Float order matters: two NaNs meeting in SSE keep the first operand, so every product and sum keeps the original operand
  order (Mul/Add/Sub helpers from Frames.h). Inverse and multiply work with out aliasing an input; stores follow the
  original order.

## Lane TS: the rest of the compgeom calls the scenegraph made (src/compgeom)

- Ours now (tests/compgeom_ts.cpp, db/status/CompGeomRigid / CompGeomMatrix4 / CompGeomConvex): 0x19cd80
  RigidTransform3::Invert (conjugate quaternion, translation rotated by it, then negated; hooked), 0x1a5660
  EulerAnglesToMatrix3 (hooked; cos/sin of the double cast to float match the original's libm bit for bit, NaN and inf
  included), 0x19d2f0 BoundBox::Contains (strict compares, so a NaN passes; no hook, only the octree's ours call it) and the
  translated cuboid shape's constructor 0x18fc10 (extents halved, centre copied) / destructor 0x18fce0 (stack objects of the
  octree queries; in effect through callers, the slot-45 cycle in callers_handled). 0x190c40 (shared_vf_190c40) is reproduced
  by our class and never hooked.
- The octree test skips the shape's vtable word through the new `G` argument kind of tests/drawfx_core.h.

## Left (from the backlog, 2026-11-03)

- Small math callees (TP): 0x19c1c0 (99 B), 0x19c800 RigidRotateVector (19 B, tentative); with 0x19c0e0 / 0x1a6410 / 0x1a7200 (tentative matrix helpers) a math cluster lane.
- src/rz/COMDirectorData.cpp:49 still carries the 0x197740 literal (frustum family, ours per lane PV): switch to our function when that file is next touched.
