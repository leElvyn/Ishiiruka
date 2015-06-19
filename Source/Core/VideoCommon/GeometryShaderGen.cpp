// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cmath>

#include "VideoCommon/GeometryShaderGen.h"
#include "VideoCommon/LightingShaderGen.h"
#include "VideoCommon/VertexShaderGen.h"
#include "VideoCommon/VideoConfig.h"

static char text[GEOMETRYSHADERGEN_BUFFERSIZE];

static const char* primitives_ogl[] =
{
	"points",
	"lines",
	"triangles"
};

static const char* primitives_d3d[] =
{
	"point",
	"line",
	"triangle"
};

template<class T, API_TYPE ApiType> static inline void EmitVertex(T& out, const char* vertex, bool first_vertex, bool enable_pl, const XFMemory &xfr);
template<class T, API_TYPE ApiType> static inline void EndPrimitive(T& out, bool enable_pl, const XFMemory &xfr);

template<class T, API_TYPE ApiType, bool is_writing_shadercode>
static inline void GenerateGeometryShader(T& out, u32 primitive_type, const XFMemory &xfr)
{
	// Non-uid template parameters will write to the dummy data (=> gets optimized out)
	geometry_shader_uid_data dummy_data;
	bool uidPresent = (&out.template GetUidData<geometry_shader_uid_data>() != nullptr);
	geometry_shader_uid_data& uid_data = uidPresent ? out.template GetUidData<geometry_shader_uid_data>() : dummy_data;
	
	if (uidPresent)
	{
		out.ClearUID();
	}

	uid_data.primitive_type = primitive_type;
	const unsigned int vertex_in = primitive_type + 1;
	unsigned int vertex_out = primitive_type == PRIMITIVE_TRIANGLES ? 3 : 4;

	uid_data.wireframe = g_ActiveConfig.bWireFrame;
	if (g_ActiveConfig.bWireFrame)
		vertex_out++;

	uid_data.stereo = g_ActiveConfig.iStereoMode > 0;

	uid_data.numTexGens = xfr.numTexGen.numTexGens;
	bool lightingEnabled = xfr.numChan.numColorChans > 0 && g_ActiveConfig.bEnablePixelLighting
		&& g_ActiveConfig.backend_info.bSupportsPixelLighting;
	uid_data.pixel_lighting = lightingEnabled;

	char* codebuffer = nullptr;
	if (is_writing_shadercode)
	{
		codebuffer = out.GetBuffer();
		if (codebuffer == nullptr)
		{
			codebuffer = text;
			out.SetBuffer(codebuffer);
		}
		codebuffer[sizeof(text) - 1] = 0x7C;  // canary
	}
	else
	{
		return;
	}

	if (ApiType == API_OPENGL)
	{
		// Insert layout parameters
		if (g_ActiveConfig.backend_info.bSupportsGSInstancing)
		{
			out.Write("layout(%s, invocations = %d) in;\n", primitives_ogl[primitive_type], g_ActiveConfig.iStereoMode > 0 ? 2 : 1);
			out.Write("layout(%s_strip, max_vertices = %d) out;\n", g_ActiveConfig.bWireFrame ? "line" : "triangle", vertex_out);
		}
		else
		{
			out.Write("layout(%s) in;\n", primitives_ogl[primitive_type]);
			out.Write("layout(%s_strip, max_vertices = %d) out;\n", g_ActiveConfig.bWireFrame ? "line" : "triangle", g_ActiveConfig.iStereoMode > 0 ? vertex_out * 2 : vertex_out);
		}
	}

	// uniforms
	if (ApiType == API_OPENGL)
		out.Write("layout(std140%s) uniform GSBlock {\n", g_ActiveConfig.backend_info.bSupportsBindingLayout ? ", binding = 3" : "");
	else
		out.Write("cbuffer GSBlock {\n");
	out.Write(
		"\tfloat4 " I_STEREOPARAMS";\n"
		"\tfloat4 " I_LINEPTPARAMS";\n"
		"\tint4 " I_TEXOFFSET";\n"
		"};\n");

	

	out.Write("struct VS_OUTPUT {\n");
	GenerateVSOutputMembers<T, ApiType>(out, lightingEnabled, xfr);
	out.Write("};\n");

	if (ApiType == API_OPENGL)
	{
		if (g_ActiveConfig.backend_info.bSupportsGSInstancing)
			out.Write("#define InstanceID gl_InvocationID\n");

		out.Write("in VertexData {\n");
		GenerateVSOutputMembers<T, ApiType>(out, lightingEnabled, xfr, g_ActiveConfig.backend_info.bSupportsBindingLayout ? "centroid" : "centroid in");
		out.Write("} vs[%d];\n", vertex_in);

		out.Write("out VertexData {\n");
		GenerateVSOutputMembers<T, ApiType>(out, lightingEnabled, xfr, g_ActiveConfig.backend_info.bSupportsBindingLayout ? "centroid" : "centroid out");

		if (g_ActiveConfig.iStereoMode > 0)
			out.Write("\tflat int layer;\n");

		out.Write("} ps;\n");

		out.Write("void main()\n{\n");
	}
	else // D3D
	{
		out.Write("struct VertexData {\n");
		out.Write("\tVS_OUTPUT o;\n");

		if (g_ActiveConfig.iStereoMode > 0)
			out.Write("\tuint layer : SV_RenderTargetArrayIndex;\n");

		out.Write("};\n");

		if (g_ActiveConfig.backend_info.bSupportsGSInstancing)
		{
			out.Write("[maxvertexcount(%d)]\n[instance(%d)]\n", vertex_out, g_ActiveConfig.iStereoMode > 0 ? 2 : 1);
			out.Write("void main(%s VS_OUTPUT o[%d], inout %sStream<VertexData> output, in uint InstanceID : SV_GSInstanceID)\n{\n", primitives_d3d[primitive_type], vertex_in, g_ActiveConfig.bWireFrame ? "Line" : "Triangle");
		}
		else
		{
			out.Write("[maxvertexcount(%d)]\n", g_ActiveConfig.iStereoMode > 0 ? vertex_out * 2 : vertex_out);
			out.Write("void main(%s VS_OUTPUT o[%d], inout %sStream<VertexData> output)\n{\n", primitives_d3d[primitive_type], vertex_in, g_ActiveConfig.bWireFrame ? "Line" : "Triangle");
		}

		out.Write("\tVertexData ps;\n");
	}

	if (primitive_type == PRIMITIVE_LINES)
	{
		if (ApiType == API_OPENGL)
		{
			out.Write("\tVS_OUTPUT start, end;\n");
			AssignVSOutputMembers<T, ApiType>(out, "start", "vs[0]", lightingEnabled, xfr);
			AssignVSOutputMembers<T, ApiType>(out, "end", "vs[1]", lightingEnabled, xfr);
		}
		else
		{
			out.Write("\tVS_OUTPUT start = o[0];\n");
			out.Write("\tVS_OUTPUT end = o[1];\n");
		}

		// GameCube/Wii's line drawing algorithm is a little quirky. It does not
		// use the correct line caps. Instead, the line caps are vertical or
		// horizontal depending the slope of the line.
		out.Write(
			"\tfloat2 offset;\n"
			"\tfloat2 to = abs(end.pos.xy / end.pos.w - start.pos.xy / start.pos.w);\n"
			// FIXME: What does real hardware do when line is at a 45-degree angle?
			// FIXME: Lines aren't drawn at the correct width. See Twilight Princess map.
			"\tif (" I_LINEPTPARAMS".y * to.y > " I_LINEPTPARAMS".x * to.x) {\n"
			// Line is more tall. Extend geometry left and right.
			// Lerp LineWidth/2 from [0..VpWidth] to [-1..1]
			"\t\toffset = float2(" I_LINEPTPARAMS".z / " I_LINEPTPARAMS".x, 0);\n"
			"\t} else {\n"
			// Line is more wide. Extend geometry up and down.
			// Lerp LineWidth/2 from [0..VpHeight] to [1..-1]
			"\t\toffset = float2(0, -" I_LINEPTPARAMS".z / " I_LINEPTPARAMS".y);\n"
			"\t}\n");
	}
	else if (primitive_type == PRIMITIVE_POINTS)
	{
		if (ApiType == API_OPENGL)
		{
			out.Write("\tVS_OUTPUT center;\n");
			AssignVSOutputMembers<T, ApiType>(out, "center", "vs[0]", lightingEnabled, xfr);
		}
		else
		{
			out.Write("\tVS_OUTPUT center = o[0];\n");
		}

		// Offset from center to upper right vertex
		// Lerp PointSize/2 from [0,0..VpWidth,VpHeight] to [-1,1..1,-1]
		out.Write("\tfloat2 offset = float2(" I_LINEPTPARAMS".w / " I_LINEPTPARAMS".x, -" I_LINEPTPARAMS".w / " I_LINEPTPARAMS".y) * center.pos.w;\n");
	}

	if (g_ActiveConfig.iStereoMode > 0)
	{
		// If the GPU supports invocation we don't need a for loop and can simply use the
		// invocation identifier to determine which layer we're rendering.
		if (g_ActiveConfig.backend_info.bSupportsGSInstancing)
			out.Write("\tint eye = InstanceID;\n");
		else
			out.Write("\tfor (int eye = 0; eye < 2; ++eye) {\n");
	}

	if (g_ActiveConfig.bWireFrame)
		out.Write("\tVS_OUTPUT first;\n");

	out.Write("\tfor (int i = 0; i < %d; ++i) {\n", vertex_in);

	if (ApiType == API_OPENGL)
	{
		out.Write("\tVS_OUTPUT f;\n");
		AssignVSOutputMembers<T, ApiType>(out, "f", "vs[i]", lightingEnabled, xfr);
	}
	else
	{
		out.Write("\tVS_OUTPUT f = o[i];\n");
	}

	if (g_ActiveConfig.iStereoMode > 0)
	{
		// Select the output layer
		out.Write("\tps.layer = eye;\n");
		if (ApiType == API_OPENGL)
			out.Write("\tgl_Layer = eye;\n");

		// For stereoscopy add a small horizontal offset in Normalized Device Coordinates proportional
		// to the depth of the vertex. We retrieve the depth value from the w-component of the projected
		// vertex which contains the negated z-component of the original vertex.
		// For negative parallax (out-of-screen effects) we subtract a convergence value from
		// the depth value. This results in objects at a distance smaller than the convergence
		// distance to seemingly appear in front of the screen.
		// This formula is based on page 13 of the "Nvidia 3D Vision Automatic, Best Practices Guide"
		out.Write("\tf.pos.x += " I_STEREOPARAMS"[eye] * (f.pos.w - " I_STEREOPARAMS"[2]);\n");
	}

	if (primitive_type == PRIMITIVE_LINES)
	{
		out.Write("\tVS_OUTPUT l = f;\n"
			"\tVS_OUTPUT r = f;\n");

		out.Write("\tl.pos.xy -= offset * l.pos.w;\n"
			"\tr.pos.xy += offset * r.pos.w;\n");

		out.Write("\tif (" I_TEXOFFSET"[2] != 0) {\n");
		out.Write("\tfloat texOffset = 1.0 / float(" I_TEXOFFSET"[2]);\n");

		for (unsigned int i = 0; i < xfr.numTexGen.numTexGens; ++i)
		{
			out.Write("\tif (((" I_TEXOFFSET"[0] >> %d) & 0x1) != 0)\n", i);
			out.Write("\t\tr.tex%d.x += texOffset;\n", i);
		}
		out.Write("\t}\n");

		EmitVertex<T, ApiType>(out, "l", true, lightingEnabled, xfr);
		EmitVertex<T, ApiType>(out, "r", false, lightingEnabled, xfr);
	}
	else if (primitive_type == PRIMITIVE_POINTS)
	{
		out.Write("\tVS_OUTPUT ll = f;\n"
			"\tVS_OUTPUT lr = f;\n"
			"\tVS_OUTPUT ul = f;\n"
			"\tVS_OUTPUT ur = f;\n");

		out.Write("\tll.pos.xy += float2(-1,-1) * offset;\n"
			"\tlr.pos.xy += float2(1,-1) * offset;\n"
			"\tul.pos.xy += float2(-1,1) * offset;\n"
			"\tur.pos.xy += offset;\n");

		out.Write("\tif (" I_TEXOFFSET"[3] != 0) {\n");
		out.Write("\tfloat2 texOffset = float2(1.0 / float(" I_TEXOFFSET"[3]), 1.0 / float(" I_TEXOFFSET"[3]));\n");

		for (unsigned int i = 0; i < xfr.numTexGen.numTexGens; ++i)
		{
			out.Write("\tif (((" I_TEXOFFSET"[1] >> %d) & 0x1) != 0) {\n", i);
			out.Write("\t\tll.tex%d.xy += float2(0,1) * texOffset;\n", i);
			out.Write("\t\tlr.tex%d.xy += texOffset;\n", i);
			out.Write("\t\tur.tex%d.xy += float2(1,0) * texOffset;\n", i);
			out.Write("\t}\n");
		}
		out.Write("\t}\n");

		EmitVertex<T, ApiType>(out, "ll", true, lightingEnabled, xfr);
		EmitVertex<T, ApiType>(out, "lr", false, lightingEnabled, xfr);
		EmitVertex<T, ApiType>(out, "ul", false, lightingEnabled, xfr);
		EmitVertex<T, ApiType>(out, "ur", false, lightingEnabled, xfr);
	}
	else
	{
		EmitVertex<T, ApiType>(out, "f", true, lightingEnabled, xfr);
	}

	out.Write("\t}\n");

	EndPrimitive<T, ApiType>(out, lightingEnabled, xfr);

	if (g_ActiveConfig.iStereoMode > 0 && !g_ActiveConfig.backend_info.bSupportsGSInstancing)
		out.Write("\t}\n");

	out.Write("}\n");

	if (is_writing_shadercode)
	{
		if (codebuffer[GEOMETRYSHADERGEN_BUFFERSIZE - 1] != 0x7C)
			PanicAlert("GeometryShader generator - buffer too small, canary has been eaten!");
	}
}

template<class T, API_TYPE ApiType>
static inline void EmitVertex(T& out, const char* vertex, bool first_vertex, bool enable_pl, const XFMemory &xfr)
{
	if (g_ActiveConfig.bWireFrame && first_vertex)
		out.Write("\tif (i == 0) first = %s;\n", vertex);

	if (ApiType == API_OPENGL)
	{
		out.Write("\tgl_Position = %s.pos;\n", vertex);
		AssignVSOutputMembers<T, ApiType>(out, "ps", vertex, enable_pl, xfr);
	}
	else
	{
		out.Write("\tps.o = %s;\n", vertex);
	}

	if (ApiType == API_OPENGL)
		out.Write("\tEmitVertex();\n");
	else
		out.Write("\toutput.Append(ps);\n");
}
template<class T, API_TYPE ApiType>
static inline void EndPrimitive(T& out, bool enable_pl, const XFMemory &xfr)
{
	if (g_ActiveConfig.bWireFrame)
		EmitVertex<T, ApiType>(out, "first", false, enable_pl, xfr);

	if (ApiType == API_OPENGL)
		out.Write("\tEndPrimitive();\n");
	else
		out.Write("\toutput.RestartStrip();\n");
}

void GetGeometryShaderUid(GeometryShaderUid& object, u32 primitive_type, API_TYPE ApiType, const XFMemory &xfr)
{
	if (ApiType == API_OPENGL)
	{
		GenerateGeometryShader<GeometryShaderUid, API_OPENGL, false>(object, primitive_type, xfr);
	}
	else
	{
		GenerateGeometryShader<GeometryShaderUid, API_D3D11, false>(object, primitive_type, xfr);
	}
}

void GenerateGeometryShaderCode(ShaderCode& object, u32 primitive_type, API_TYPE ApiType, const XFMemory &xfr)
{
	if (ApiType == API_OPENGL)
	{
		GenerateGeometryShader<ShaderCode, API_OPENGL, true>(object, primitive_type, xfr);
	}
	else
	{
		GenerateGeometryShader<ShaderCode, API_D3D11, true>(object, primitive_type, xfr);
	}
}
