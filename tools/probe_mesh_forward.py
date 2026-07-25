"""Measure which way the IMPORTED skeletal mesh faces, in UE space.

The w32-2 fix derived the forward axis from the BLENDER file (jaw at +Y) and got
the sign wrong in game, because the FBX exporter + importer apply an axis
conversion between the two. Measuring the Blender file and reasoning about UE is
the bug. This measures the asset UE actually has.
"""
import unreal

sk = unreal.EditorAssetLibrary.load_asset('/PrimalFauna/Sicklejaw/SK_Sicklejaw')
skel = sk.get_editor_property('skeleton')
ref = skel.get_reference_pose() if hasattr(skel, 'get_reference_pose') else None
unreal.log('PFAFWD skeleton=%s' % skel.get_name())

# Component-space bone positions from the reference skeleton.
for bone in ('Head', 'Jaw', 'Neck_03', 'Hip', 'Tail01', 'Tail05', 'Root'):
    try:
        t = unreal.SkeletalMeshLibrary if False else None
        idx = skel.get_editor_property('bone_tree')
        pos = None
        # Preferred API: per-bone component-space transform off the mesh.
        pos = unreal.SkeletalMeshLibrary.get_bone_transform(sk, bone) if hasattr(unreal, 'SkeletalMeshLibrary') else None
        if pos is not None:
            loc = pos.translation
            unreal.log('PFAFWD %-9s x=%+8.2f y=%+8.2f z=%+8.2f' % (bone, loc.x, loc.y, loc.z))
    except Exception as e:
        unreal.log('PFAFWD %-9s <unavailable: %s>' % (bone, e))

b = sk.get_bounds()
unreal.log('PFAFWD bounds origin=(%.1f, %.1f, %.1f) extent=(%.1f, %.1f, %.1f)' % (
    b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z))
unreal.log('PFAFWD DONE')
