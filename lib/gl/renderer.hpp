// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443271. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the GLVis visualization tool and library. For more
// information and source code availability see http://glvis.org.
//
// GLVis is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#ifndef GLVIS_RENDERER
#define GLVIS_RENDERER

#include <memory>
#include <vector>
#include <set>
#include <unordered_map>

#include "platform_gl.hpp"
#include "types.hpp"
#include "../material.hpp"

namespace gl3
{

const int LIGHTS_MAX = 3;
#ifdef GLVIS_MS_LINEWIDTH
const float LINE_WIDTH_AA = GLVIS_MS_LINEWIDTH;
#else
const float LINE_WIDTH_AA = 1.4;
#endif

struct RenderParams
{
   // Transformation matrices
   GlMatrix model_view;
   GlMatrix projection;

   // Lighting settings
   Material mesh_material;
   int num_pt_lights;
   std::array<Light, LIGHTS_MAX> lights;
   std::array<float, 4> light_amb_scene;
   std::array<float, 4> static_color;

   // Clip plane parameters
   bool use_clip_plane;
   std::array<double, 4> clip_plane_eqn;

   // If true, batch contains translucent drawables
   bool contains_translucent;
};

typedef vector<pair<RenderParams, GlDrawable*>> RenderQueue;

struct SceneInfo
{
   vector<GlDrawable*> needs_buffering;
   RenderQueue queue;
};

struct FeedbackVertex
{
   glm::vec3 position;
   glm::vec4 color;

   FeedbackVertex() = default;
   FeedbackVertex(glm::vec3 pos, glm::vec4 c)
      : position(pos), color(c) { }
   FeedbackVertex(glm::vec4 pos, glm::vec4 c)
      : position(pos), color(c) { }
};

struct FeedbackText
{
   glm::vec3 offset;
   glm::vec4 color;
   std::string text;

   FeedbackText() = default;
   FeedbackText(glm::vec3 t_off, glm::vec4 t_color, std::string txt)
      : offset(t_off), color(t_color), text(std::move(txt)) { }
};

struct CaptureBuffer
{
   vector<FeedbackVertex> lines;
   vector<FeedbackVertex> triangles;
   vector<FeedbackText> text;
};

// OpenGL device interface representing rendering capabilities
class GLDevice
{
protected:
   int vp_width;
   int vp_height;
   glm::mat4 model_view_mtx;
   glm::mat4 proj_mtx;

   std::array<float, 4> static_color;

   // RAII scope guard for OpenGL handles.
   template<void(*GLFinalizer)(GLuint)>
   class Handle
   {
      GLuint hnd;
   public:
      Handle() : hnd{0} {}
      Handle(GLuint h) : hnd{h} {}
      ~Handle() { if (hnd) { GLFinalizer(hnd); } }
      Handle(Handle&& other)
         : hnd{other.hnd} { other.hnd = 0; }
      Handle& operator = (Handle&& other) noexcept
      {
         if (this != &other)
         {
            hnd = other.hnd;
            other.hnd = 0;
         }
         return *this;
      }
      operator GLuint() { return hnd; }
   };

   static void boCleanup(GLuint vbo_hnd)
   {
      glDeleteBuffers(1, &vbo_hnd);
   }

   static void dspListCleanup(GLuint dlist)
   {
      glDeleteLists(dlist, 1);
   }

   static void prgmCleanup(GLuint prgm)
   {
      glDeleteProgram(prgm);
   }

   static void vaoCleanup(GLuint vao)
   {
      glDeleteVertexArrays(1, &vao);
   }

   using BufObjHandle = Handle<boCleanup>;
   using DispListHandle = Handle<dspListCleanup>;
   using VtxArrayHandle = Handle<vaoCleanup>;
   using ShaderPrgmHandle = Handle<prgmCleanup>;

public:

   enum DeviceType
   {
      NO_DEVICE,
      FF_DEVICE,
      CORE_DEVICE
   };

   virtual ~GLDevice() = default;
   const static int SAMPLER_COLOR = 0;
   const static int SAMPLER_ALPHA = 1;

   void detachTexture(int tex_unit)
   {
      glActiveTexture(GL_TEXTURE0 + tex_unit);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   void attachTexture(int tex_unit, int tex_id)
   {
      glActiveTexture(GL_TEXTURE0 + tex_unit);
      glBindTexture(GL_TEXTURE_2D, tex_id);
   };

   void enableBlend() { glEnable(GL_BLEND); }
   void disableBlend() { glDisable(GL_BLEND); }
   void enableDepthWrite() { glDepthMask(GL_TRUE); }
   void disableDepthWrite() { glDepthMask(GL_FALSE); }
   void enableMultisample()
   {
      glEnable(GL_MULTISAMPLE);
      glEnable(GL_LINE_SMOOTH);
   }
   void disableMultisample()
   {
      glDisable(GL_MULTISAMPLE);
      glDisable(GL_LINE_SMOOTH);
   }
   void setLineWidth(float w) { glLineWidth(w); }

   virtual void init();
   virtual DeviceType getType() = 0;
   // Sets the window viewport.
   void setViewport(GLsizei w, GLsizei h);
   // Gets the current window viewport.
   void getViewport(GLint (&vp)[4]);
   // Set the color to use, if a color attribute is not provided.
   void setStaticColor(const std::array<float, 4>& rgba) { static_color = rgba; }

   // === Render pipeline functions ===

   // Set the current transform matrices.
   virtual void setTransformMatrices(glm::mat4 model_view, glm::mat4 projection);
   // Set the number of lights to use. Setting number of lights to 0 disables
   // lighting.
   virtual void setNumLights(int i) = 0;
   // Set the parameters to use for the mesh material.
   virtual void setMaterial(Material mat) = 0;
   // Set the array of parameters to use for each light.
   virtual void setPointLight(int i, Light lt) = 0;
   // Set the color value of the global ambient light.
   virtual void setAmbientLight(const std::array<float, 4>& amb) = 0;
   // Set whether to enable or disable the clip plane.
   virtual void setClipPlaneUse(bool enable) = 0;
   // Set the equation to use for the clip plane.
   virtual void setClipPlaneEqn(const std::array<double, 4>& eqn) = 0;

   // === Buffer management functions ===

   // Load a client-side vertex buffer into a device buffer.
   virtual void bufferToDevice(array_layout layout, IVertexBuffer& buf) = 0;
   virtual void bufferToDevice(array_layout layout, IIndexedBuffer& buf) = 0;
   virtual void bufferToDevice(TextBuffer& t_buf) = 0;
   // Draw the data loaded in a device buffer.
   virtual void drawDeviceBuffer(int hnd) = 0;
   virtual void drawDeviceBuffer(const TextBuffer& t_buf) = 0;

   // === Transform feedback functions ===

   // Initializes state needed for transform feedback.
   virtual void initXfbMode() {}
   // Prepares state when exiting transform feedback.
   virtual void exitXfbMode() {}
   // Capture the next drawn vertex buffer to a feedback buffer instead of
   // drawing to screen.
   virtual void captureXfbBuffer(CaptureBuffer& capture, int hnd) = 0;
   // Capture the next text buffer instead of drawing to screen.
   void captureXfbBuffer(CaptureBuffer& capture, const TextBuffer& t_buf);

};

class MeshRenderer
{
   unique_ptr<GLDevice> device;
   bool msaa_enable;
   GLuint color_tex, alpha_tex, font_tex;
   float line_w, line_w_aa;
public:
   MeshRenderer()
      : msaa_enable(false)
      , line_w(1.f)
      , line_w_aa(LINE_WIDTH_AA) { }

   template<typename TDevice>
   void setDevice()
   {
      device.reset(new TDevice());
      device->setLineWidth(line_w);
      device->init();
      msaa_enable = false;
   }

   template<typename TDevice>
   void setDevice(TDevice&& device)
   {
      device.reset(new TDevice(device));
   }

   GLenum getDeviceAlphaChannel();

   // Sets the texture handle of the color palette.
   void setColorTexture(GLuint tex_h) { color_tex = tex_h; }
   // Sets the texture handle of the alpha texture.
   void setAlphaTexture(GLuint tex_h) { alpha_tex = tex_h; }
   // Sets the texture handle of the font atlas.
   void setFontTexture(GLuint tex_h) { font_tex = tex_h; }

   void setAntialiasing(bool aa_status);
   bool getAntialiasing() { return msaa_enable; }

   void setLineWidth(float w);
   float getLineWidth() { return line_w; }
   void setLineWidthMS(float w);
   float getLineWidthMS() { return line_w_aa; }

   void setClearColor(float r, float g, float b, float a) { glClearColor(r, g, b, a); }
   void setViewport(GLsizei w, GLsizei h) { device->setViewport(w, h); }

   void init();
   void render(const RenderQueue& queued);
   CaptureBuffer capture(const RenderQueue& queued);

   void buffer(GlDrawable* buf);
};

}

#endif // GLVIS_RENDERER