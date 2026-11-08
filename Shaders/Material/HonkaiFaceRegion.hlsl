// Bind-pose directions keep the underside boundary fixed through head motion.
// Backward-facing undersides receive neck lighting; frontal nose/lip normals retain SDF.
float3 normal = normalize(RestNormal);
float underside = step(normal.z, -0.4) * step(normal.y, 0.0) * step(0.5, NoseMask);
return Enabled * (1 - underside);
