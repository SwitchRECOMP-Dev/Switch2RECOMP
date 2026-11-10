#version 440 core
layout(std430, binding=45) readonly buffer SWR_MV { uint d[]; } swr_mv;
uniform vec4 swr_vp;
out gl_PerVertex { invariant vec4 gl_Position; float gl_PointSize; float gl_ClipDistance[8]; };
layout(location=1) out vec4 out_attr1;
void main(){
  uint b=uint(gl_VertexID)*6u;
  gl_PointSize=1.0; gl_Position=vec4(0.0,0.0,0.0,1.0);
  out_attr1=vec4(0.0,0.0,0.0,1.0);
  gl_Position.x=uintBitsToFloat(swr_mv.d[b+0u]);
  gl_Position.y=uintBitsToFloat(swr_mv.d[b+1u]);
  gl_Position.z=uintBitsToFloat(swr_mv.d[b+2u]);
  gl_Position.w=uintBitsToFloat(swr_mv.d[b+3u]);
  out_attr1.x=uintBitsToFloat(swr_mv.d[b+4u]);
  out_attr1.y=uintBitsToFloat(swr_mv.d[b+5u]);
  gl_Position.y *= swr_vp.x; if(swr_vp.y!=0.0) gl_Position.z = gl_Position.z*2.0 - gl_Position.w;
}
