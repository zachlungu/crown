/*
 * Copyright (c) 2012-2016 Daniele Bartolini and individual contributors.
 * License: https://github.com/taylor001/crown/blob/master/LICENSE
 */

#include "aabb.h"
#include "compile_options.h"
#include "dynamic_string.h"
#include "filesystem.h"
#include "log.h"
#include "map.h"
#include "matrix4x4.h"
#include "mesh_resource.h"
#include "reader_writer.h"
#include "resource_manager.h"
#include "sjson.h"
#include "temp_allocator.h"
#include "vector.h"
#include "vector2.h"
#include "vector3.h"

namespace crown
{
namespace mesh_resource
{
	struct MeshCompiler
	{
		CompileOptions& _opts;

		Array<float> _positions;
		Array<float> _normals;
		Array<float> _uvs;
		Array<float> _tangents;
		Array<float> _binormals;

		Array<uint16_t> _position_indices;
		Array<uint16_t> _normal_indices;
		Array<uint16_t> _uv_indices;
		Array<uint16_t> _tangent_indices;
		Array<uint16_t> _binormal_indices;

		uint32_t _vertex_stride;
		Array<char> _vertex_buffer;
		Array<uint16_t> _index_buffer;

		AABB _aabb;
		OBB _obb;

		bgfx::VertexDecl _decl;

		bool _has_normal;
		bool _has_uv;

		MeshCompiler(CompileOptions& opts)
			: _opts(opts)
			, _positions(default_allocator())
			, _normals(default_allocator())
			, _uvs(default_allocator())
			, _tangents(default_allocator())
			, _binormals(default_allocator())
			, _position_indices(default_allocator())
			, _normal_indices(default_allocator())
			, _uv_indices(default_allocator())
			, _tangent_indices(default_allocator())
			, _binormal_indices(default_allocator())
			, _vertex_stride(0)
			, _vertex_buffer(default_allocator())
			, _index_buffer(default_allocator())
			, _has_normal(false)
			, _has_uv(false)
		{
		}

		void reset()
		{
			array::clear(_positions);
			array::clear(_normals);
			array::clear(_uvs);
			array::clear(_tangents);
			array::clear(_binormals);

			array::clear(_position_indices);
			array::clear(_normal_indices);
			array::clear(_uv_indices);
			array::clear(_tangent_indices);
			array::clear(_binormal_indices);

			_vertex_stride = 0;
			array::clear(_vertex_buffer);
			array::clear(_index_buffer);

			aabb::reset(_aabb);

			_has_normal = false;
			_has_uv = false;
		}

		void parse(const char* json)
		{
			TempAllocator4096 ta;
			JsonObject object(ta);
			sjson::parse(json, object);

			_has_normal = map::has(object, FixedString("normal"));
			_has_uv     = map::has(object, FixedString("texcoord"));

			parse_float_array(object["position"], _positions);

			if (_has_normal)
			{
				parse_float_array(object["normal"], _normals);
			}
			if (_has_uv)
			{
				parse_float_array(object["texcoord"], _uvs);
			}

			parse_indices(object["indices"]);
		}

		void parse_float_array(const char* array_json, Array<float>& output)
		{
			TempAllocator4096 ta;
			JsonArray array(ta);
			sjson::parse_array(array_json, array);

			array::resize(output, array::size(array));
			for (uint32_t i = 0; i < array::size(array); ++i)
			{
				output[i] = sjson::parse_float(array[i]);
			}
		}

		void parse_index_array(const char* array_json, Array<uint16_t>& output)
		{
			TempAllocator4096 ta;
			JsonArray array(ta);
			sjson::parse_array(array_json, array);

			array::resize(output, array::size(array));
			for (uint32_t i = 0; i < array::size(array); ++i)
			{
				output[i] = (uint16_t)sjson::parse_int(array[i]);
			}
		}

		void parse_indices(const char* json)
		{
			TempAllocator4096 ta;
			JsonObject object(ta);
			sjson::parse(json, object);

			JsonArray data_json(ta);
			sjson::parse_array(object["data"], data_json);

			parse_index_array(data_json[0], _position_indices);

			if (_has_normal)
			{
				parse_index_array(data_json[1], _normal_indices);
			}
			if (_has_uv)
			{
				parse_index_array(data_json[2], _uv_indices);
			}
		}

		void compile()
		{
			_vertex_stride = 0;
			_vertex_stride += 3 * sizeof(float);
			_vertex_stride += (_has_normal ? 3 * sizeof(float) : 0);
			_vertex_stride += (_has_uv     ? 2 * sizeof(float) : 0);

			// Generate vb/ib
			array::resize(_index_buffer, array::size(_position_indices));

			uint16_t index = 0;
			for (uint32_t i = 0; i < array::size(_position_indices); ++i)
			{
				_index_buffer[i] = index++;

				const uint16_t p_idx = _position_indices[i] * 3;
				Vector3 xyz;
				xyz.x = _positions[p_idx + 0];
				xyz.y = _positions[p_idx + 1];
				xyz.z = _positions[p_idx + 2];
				array::push(_vertex_buffer, (char*)&xyz, sizeof(xyz));

				if (_has_normal)
				{
					const uint16_t n_idx = _normal_indices[i] * 3;
					Vector3 n;
					n.x = _normals[n_idx + 0];
					n.y = _normals[n_idx + 1];
					n.z = _normals[n_idx + 2];
					array::push(_vertex_buffer, (char*)&n, sizeof(n));
				}
				if (_has_uv)
				{
					const uint16_t t_idx = _uv_indices[i] * 2;
					Vector2 uv;
					uv.x = _uvs[t_idx + 0];
					uv.y = _uvs[t_idx + 1];
					array::push(_vertex_buffer, (char*)&uv, sizeof(uv));
				}
			}

			// Vertex decl
			_decl.begin();
			_decl.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float);

			if (_has_normal)
			{
				_decl.add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float, true);
			}
			if (_has_uv)
			{
				_decl.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
			}

			_decl.end();

			// Buonds
			aabb::reset(_aabb);
			aabb::add_points(_aabb
				, array::size(_positions) / 3
				, sizeof(float) * 3
				, array::begin(_positions)
				);

			_obb.tm = matrix4x4(QUATERNION_IDENTITY, aabb::center(_aabb));
			_obb.half_extents.x = (_aabb.max.x - _aabb.min.x) * 0.5f;
			_obb.half_extents.y = (_aabb.max.y - _aabb.min.y) * 0.5f;
			_obb.half_extents.z = (_aabb.max.z - _aabb.min.z) * 0.5f;
		}

		void write()
		{
			_opts.write(_decl);
			_opts.write(_obb);

			_opts.write(array::size(_vertex_buffer) / _vertex_stride);
			_opts.write(_vertex_stride);
			_opts.write(array::size(_index_buffer));

			_opts.write(_vertex_buffer);
			_opts.write(array::begin(_index_buffer), array::size(_index_buffer) * sizeof(uint16_t));
		}
	};

	void compile(const char* path, CompileOptions& opts)
	{
		Buffer buf = opts.read(path);

		TempAllocator4096 ta;
		JsonObject object(ta);
		sjson::parse(buf, object);

		JsonObject geometries(ta);
		sjson::parse(object["geometries"], geometries);

		opts.write(MESH_VERSION);
		opts.write(map::size(geometries));

		MeshCompiler mc(opts);

		auto begin = map::begin(geometries);
		auto end = map::end(geometries);
		for (; begin != end; ++begin)
		{
			const FixedString key = begin->pair.first;
			const StringId32 name(key.data(), key.length());
			opts.write(name._id);

			mc.reset();
			mc.parse(begin->pair.second);
			mc.compile();
			mc.write();
		}
	}

	void* load(File& file, Allocator& a)
	{
		BinaryReader br(file);

		uint32_t version;
		br.read(version);
		CE_ASSERT(version == MESH_VERSION, "Wrong version");

		uint32_t num_geoms;
		br.read(num_geoms);

		MeshResource* mr = CE_NEW(a, MeshResource)(a);
		array::resize(mr->geometry_names, num_geoms);
		array::resize(mr->geometries, num_geoms);

		for (uint32_t i = 0; i < num_geoms; ++i)
		{
			StringId32 name;
			br.read(name);

			bgfx::VertexDecl decl;
			br.read(decl);

			OBB obb;
			br.read(obb);

			uint32_t num_verts;
			br.read(num_verts);

			uint32_t stride;
			br.read(stride);

			uint32_t num_inds;
			br.read(num_inds);

			const uint32_t vsize = num_verts*stride;
			const uint32_t isize = num_inds*sizeof(uint16_t);

			const uint32_t size = sizeof(MeshGeometry) + vsize + isize;

			MeshGeometry* mg = (MeshGeometry*)a.allocate(size);
			mg->obb             = obb;
			mg->decl            = decl;
			mg->vertex_buffer   = BGFX_INVALID_HANDLE;
			mg->index_buffer    = BGFX_INVALID_HANDLE;
			mg->vertices.num    = num_verts;
			mg->vertices.stride = stride;
			mg->vertices.data   = (char*)&mg[1];
			mg->indices.num     = num_inds;
			mg->indices.data    = mg->vertices.data + vsize;

			br.read(mg->vertices.data, vsize);
			br.read(mg->indices.data, isize);

			mr->geometry_names[i] = name;
			mr->geometries[i] = mg;
		}

		return mr;
	}

	void online(StringId64 id, ResourceManager& rm)
	{
		MeshResource* mr = (MeshResource*)rm.get(MESH_TYPE, id);

		for (uint32_t i = 0; i < array::size(mr->geometries); ++i)
		{
			MeshGeometry& mg = *mr->geometries[i];

			const uint32_t vsize = mg.vertices.num * mg.vertices.stride;
			const uint32_t isize = mg.indices.num * sizeof(uint16_t);

			const bgfx::Memory* vmem = bgfx::makeRef(mg.vertices.data, vsize);
			const bgfx::Memory* imem = bgfx::makeRef(mg.indices.data, isize);

			bgfx::VertexBufferHandle vbh = bgfx::createVertexBuffer(vmem, mg.decl);
			bgfx::IndexBufferHandle ibh  = bgfx::createIndexBuffer(imem);
			CE_ASSERT(bgfx::isValid(vbh), "Invalid vertex buffer");
			CE_ASSERT(bgfx::isValid(ibh), "Invalid index buffer");

			mg.vertex_buffer = vbh;
			mg.index_buffer  = ibh;
		}
	}

	void offline(StringId64 id, ResourceManager& rm)
	{
		MeshResource* mr = (MeshResource*)rm.get(MESH_TYPE, id);

		for (uint32_t i = 0; i < array::size(mr->geometries); ++i)
		{
			MeshGeometry& mg = *mr->geometries[i];
			bgfx::destroyVertexBuffer(mg.vertex_buffer);
			bgfx::destroyIndexBuffer(mg.index_buffer);
		}
	}

	void unload(Allocator& a, void* res)
	{
		MeshResource* mr = (MeshResource*)res;

		for (uint32_t i = 0; i < array::size(mr->geometries); ++i)
		{
			a.deallocate(mr->geometries[i]);
		}
		CE_DELETE(a, (MeshResource*)res);
	}
} // namespace mesh_resource
} // namespace crown