/* ---------------------------------------------------------------------------
 * Copyright (C) 2017 Liangliang Nan <liangliang.nan@gmail.com>
 * https://3d.bk.tudelft.nl/liangliang/
 *
 * This file is part of PolyFit. If it is useful in your research/work,
 * I would be grateful if you show your appreciation by citing it:
 *
 *     Liangliang Nan and Peter Wonka.
 *     PolyFit: Polygonal Surface Reconstruction from Point Clouds.
 *     ICCV 2017.
 *
 *  For more information:
 *  https://3d.bk.tudelft.nl/liangliang/publications/2017/polyfit/polyfit.html
 * ---------------------------------------------------------------------------
 */

#include "paint_canvas.h"

#include <QMessageBox>
#include <QCoreApplication>

#include <3rd_party/QGLViewer/QGLViewer/manipulatedCameraFrame.h>
#include <basic/file_utils.h>
#include <basic/stop_watch.h>
#include <model/map_editor.h>
#include <model/map_geometry.h>
#include <renderer/opengl_info.h>
#include <renderer/surface_render.h>
#include <renderer/point_set_render.h>
#include <method/hypothesis_generator.h>
#include <method/face_selection.h>
#include <method/method_global.h>

#include "main_window.h"


using namespace qglviewer;

PaintCanvas::PaintCanvas(QWidget *parent)
	: QGLViewer(parent)
	, coord_system_region_size_(150)
	, show_coord_sys_(true)
	, point_set_(nil)
	, hypothesis_mesh_(nil)
	, show_input_(true)
	, show_candidates_(true)
	, show_result_(true)
	, show_hint_text_(true)
	, show_mouse_hint_(false)
	, hypothesis_(nil)
{
	setFPSIsDisplayed(true);

	main_window_ = dynamic_cast<MainWindow*>(parent);

	light_pos_ = vec3(0.27f, 0.27f, 0.92f);

	//////////////////////////////////////////////////////////////////////////

	// Move camera according to viewer type (on X, Y or Z axis)
	camera()->setPosition(qglviewer::Vec(1.0, 1.0, 1.0));

	camera()->lookAt(sceneCenter());
	camera()->setType(qglviewer::Camera::PERSPECTIVE);
	camera()->showEntireScene();

	mesh_render_ = new SurfaceRender(this);
	point_set_render_ = new PointSetRender(this);

	hint_text_ = "To start, click \'Open\' to load a point cloud.";
	hint_text2nd_ = "";
}


PaintCanvas::~PaintCanvas() {
//	// this is required by the following destruction of textures, shaders, etc.
//	makeCurrent();

	delete point_set_render_;
	delete mesh_render_;

	clear();
}


void PaintCanvas::clear() {
	if (point_set_)
		point_set_.forget();

	if (hypothesis_mesh_)
		hypothesis_mesh_.forget();

	if (optimized_mesh_)
		optimized_mesh_.forget();

	if (hypothesis_) {
		delete hypothesis_;
		hypothesis_ = 0;
	}
}

// in case you're running PolyFit on an ancient machine where 
// OpenGL is not supported (though it is minimum). In such a
// case, all rendering will be disabled.
static bool fatal_opengl_error = false;
void PaintCanvas::init()
{
    ogf_check_gl;

	GLenum err = glewInit();
	if (GLEW_OK != err) {
		// Problem: glewInit failed, something is seriously wrong. 
		Logger::err("-") << glewGetErrorString(err) << std::endl;
		Logger::err("-") << "OpenGL error detected and rendering disabled. You are still able to run PolyFit and export the result." << std::endl;
		fatal_opengl_error = true;
	}

	//////////////////////////////////////////////////////////////////////////

	setStateFileName("");

	// Default value is 0.005, which is appropriate for most applications. 
	// A lower value will prevent clipping of very close objects at the 
	// expense of a worst Z precision.
	camera()->setZNearCoefficient(0.005f);

	// Default value is square root of 3.0 (so that a cube of size 
	// sceneRadius() is not clipped).
	camera()->setZClippingCoefficient(std::sqrt(3.0f));

	camera()->setViewDirection(qglviewer::Vec(0.0, 1.0, 0.0));
	camera()->setType(qglviewer::Camera::PERSPECTIVE);
	showEntireScene();

	camera()->frame()->setSpinningSensitivity(/*1.0f*/100.0f);
	setMouseTracking(true);

	//////////////////////////////////////////////////////////////////////////
	// I like the inverse direction
	setShortcut(MOVE_CAMERA_LEFT, Qt::Key_Right);
	setShortcut(MOVE_CAMERA_RIGHT, Qt::Key_Left);
	setShortcut(MOVE_CAMERA_UP, Qt::Key_Down);
	setShortcut(MOVE_CAMERA_DOWN, Qt::Key_Up);

	setMouseBinding(Qt::ShiftModifier, Qt::LeftButton, CAMERA, SCREEN_ROTATE);
	setMouseBinding(Qt::ControlModifier, Qt::LeftButton, CAMERA, ZOOM_ON_REGION);

	//////////////////////////////////////////////////////////////////////////

	glEnable(GL_DEPTH_TEST);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	//////////////////////////////////////////////////////////////////////////

	QColor bkgrd_color = Qt::white;
	setBackgroundColor(bkgrd_color);

	//////////////////////////////////////////////////////////////////////////

	//float pos[] = { 0.5f, 0.5f, 0.5f, 1.0f };
	float pos[] = { light_pos_.x, light_pos_.y, light_pos_.z, 0.0f };
	glLightfv(GL_LIGHT0, GL_POSITION, pos);

	// Setup light parameters
	// Liangliang: my experience is that it hurts a lot the rendering performance.
	// If not needed, you can set to "GL_FALSE" (e.g., for large scale scenes).
	glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE); /*GL_FALSE*/

	// how specular reflection angles are computed. GL_TRUE will introduce artifact for glu tess with specular
	glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_FALSE);

	// Makes specular lighting work in texture mapping mode.
	glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);

	glEnable(GL_LIGHT0);
	glEnable(GL_LIGHTING);
	glEnable(GL_NORMALIZE);

	//////////////////////////////////////////////////////////////////////////

	// specify the specular and shininess
	float specular[] = { 0.6f, 0.6f, 0.6f, 0.5f };
	float shininess = 64.0f;
	glMaterialfv(GL_FRONT, GL_SPECULAR, specular);
	glMaterialf(GL_FRONT, GL_SHININESS, shininess);

	////////////////////////////////////////////////////////////////////////////

	// 	// make the back side different
	// 	float back_specular[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	// 	float back_shininess = 128;
	// 	float ambient_back[]  = {0.0f, 1.0f, 0.0f, 1.0f};
	// 	glMaterialfv(GL_BACK, GL_SPECULAR, back_specular);
	// 	glMaterialf(GL_BACK, GL_SHININESS, back_shininess);
	// 	glMaterialfv(GL_BACK, GL_AMBIENT, ambient_back);

	////////////////////////////////////////////////////////////////////////

	// to use facet color, the GL_COLOR_MATERIAL should be enabled
	glEnable(GL_COLOR_MATERIAL);
	// to use material color, the GL_COLOR_MATERIAL should be disabled
	//glDisable(GL_COLOR_MATERIAL);

	setFPSIsDisplayed(false);
}

void PaintCanvas::draw() {
	if (fatal_opengl_error) {
		return;
	}
    ogf_check_gl;

	bool interacting = camera()->frame()->isManipulated();
    if (point_set_ && show_input_ && point_set_render_)
        point_set_render_->draw(point_set_);

	if (hypothesis_mesh_ && show_candidates_ && mesh_render_) {
		EdgeStyle s = mesh_render_->mesh_style();
		s.visible = true;
		mesh_render_->set_mesh_style(s);
		mesh_render_->draw(hypothesis_mesh_, interacting);
	}

	if (optimized_mesh_ && show_result_ && mesh_render_) {
		EdgeStyle s = mesh_render_->mesh_style();
		s.visible = false;
		mesh_render_->set_mesh_style(s);
		mesh_render_->draw(optimized_mesh_, interacting);
	}

    if (show_coord_sys_)
        drawCornerAxis();

    // Liangliang: Qt's renderText() changes some OpenGL states.
    // Save OpenGL state
    glPushAttrib(GL_ALL_ATTRIB_BITS);

    const static QFont font("Times", 12/*, QFont::Bold*/); // "Times", "Helvetica", "Bradley Hand ITC"
	if (show_hint_text_) {
		if (!hint_text_.isEmpty()) {
			glColor3f(0, 0, 0.7f);
			drawText(30, 40, hint_text_, font);
			if (!hint_text2nd_.isEmpty())
				drawText(30, 70, hint_text2nd_, font);
		}
	}

	if (show_mouse_hint_) {
		glColor3f(0, 0, 0);
		drawText(30, 100, "Mouse Operations:", font);
        glColor3f(0, 0, 0);
		drawText(30, 130, "  - Orbit: left button", font);
        glColor3f(0, 0, 0);
		drawText(30, 160, "  - Pan:   right button", font);
        glColor3f(0, 0, 0);
		drawText(30, 190, "  - Zoom:  wheel", font);
    }

    // Restore GL state
    glPopAttrib();
}


void PaintCanvas::keyPressEvent(QKeyEvent *e) {
	e->ignore();
}


void PaintCanvas::snapshotScreen(const QString& fileName) {
	bool need_hide = show_coord_sys_;
	if (need_hide)
		show_coord_sys_ = false;  // hide the coord system temporally

	show_hint_text_ = false;
	show_mouse_hint_ = false;
	setSnapshotFileName(fileName);
	setSnapshotFormat("png");
	saveImageSnapshot(fileName);
	show_hint_text_ = true;
	show_mouse_hint_ = true;

	if (need_hide)
		show_coord_sys_ = true;

	update();
}


void PaintCanvas::update_graphics() {
	update();

	// This approach has significant drawbacks. For example, imagine you wanted to perform two such loops 
	// in parallel-calling one of them would effectively halt the other until the first one is finished 
	// (so you can't distribute computing power among different tasks). It also makes the application react
	// with delays to events. Furthermore the code is difficult to read and analyze, therefore this solution
	// is only suited for short and simple problems that are to be processed in a single thread, such as 
	// splash screens and the monitoring of short operations.
	QCoreApplication::processEvents();
}

void PaintCanvas::update_all() {
	update();
	main_window_->updateStatusBar();

	// This approach has significant drawbacks. For example, imagine you wanted to perform two such loops 
	// in parallel-calling one of them would effectively halt the other until the first one is finished 
	// (so you can't distribute computing power among different tasks). It also makes the application react
	// with delays to events. Furthermore the code is difficult to read and analyze, therefore this solution
	// is only suited for short and simple problems that are to be processed in a single thread, such as 
	// splash screens and the monitoring of short operations.
	QCoreApplication::processEvents();
}


void PaintCanvas::showCoordinateSystem(bool b) {
	show_coord_sys_ = b;
	update();
}


void PaintCanvas::fitScreen() {
	Box3d box;
	if (dynamic_cast<PointSet*>(pointSet()))
		box.add_box(dynamic_cast<PointSet*>(pointSet())->bbox());
	else {
		if (dynamic_cast<Map*>(hypothesisMesh()))
			box.add_box(Geom::bounding_box(dynamic_cast<Map*>(hypothesisMesh())));
		else if (dynamic_cast<Map*>(optimizedMesh()))
			box.add_box(Geom::bounding_box(dynamic_cast<Map*>(optimizedMesh())));
	}

	qglviewer::Vec vmin(box.x_min(), box.y_min(), box.z_min());
	qglviewer::Vec vmax(box.x_max(), box.y_max(), box.z_max());

	setSceneBoundingBox(vmin, vmax);
	showEntireScene();
	update();
}


void PaintCanvas::drawCornerAxis()
{
	glEnable(GL_MULTISAMPLE);

	// The viewport and the scissor are changed to fit the lower left
	// corner. Original values are saved.
	int viewport[4];
	int scissor[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	glGetIntegerv(GL_SCISSOR_BOX, scissor);

	//////////////////////////////////////////////////////////////////////////

	// Axis viewport size, in pixels
	glViewport(0, 0, coord_system_region_size_, coord_system_region_size_);
	glScissor(0, 0, coord_system_region_size_, coord_system_region_size_);

	// The Z-buffer is cleared to make the axis appear over the
	// original image.
	glClear(GL_DEPTH_BUFFER_BIT);

	// Tune for best line rendering
	glLineWidth(3.0);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(-1, 1, -1, 1, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glMultMatrixd(camera()->orientation().inverse().matrix());

	float axis_size = 0.9f; // other 0.2 space for drawing the x, y, z labels
	drawAxis(axis_size);

	// Draw text id
	glColor3f(0, 0, 0);

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	//////////////////////////////////////////////////////////////////////////

	// The viewport and the scissor are restored.
	glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}


vec2 PaintCanvas::projectionOf(const vec3& p) {    // point to screen
	qglviewer::Vec v = camera()->projectedCoordinatesOf(qglviewer::Vec(p.x, p.y, p.z));
	return vec2(v.x, v.y);
}

vec3 PaintCanvas::unProjectionOf(double winx, double winy, double winz) {  // screen to point	
	qglviewer::Vec v = camera()->unprojectedCoordinatesOf(qglviewer::Vec(winx, winy, winz));
	return vec3(v.x, v.y, v.z);
}


void PaintCanvas::setMesh(Map* mesh) { 
	optimized_mesh_ = mesh;

	if (!point_set_)
		fitScreen();
}


void PaintCanvas::setPointSet(PointSet* pset) { 
	point_set_ = pset; 

    // assign each vertex group a random color
    // (in case the user doesn't provide color information)
    std::vector<VertexGroup::Ptr>& groups = pset->groups();
    for (auto g : groups)
        g->set_color(random_color());

	fitScreen();

	if (point_set_) {
		hint_text_ = "Next: click \'Refine\' to merge very close and near-parallel planar segments.";
		hint_text2nd_ = "";
	}

	// now I have model, show the mouse hint
	show_mouse_hint_ = true;
}


Map* PaintCanvas::hypothesisMesh() const {
	return hypothesis_mesh_;
}

Map* PaintCanvas::optimizedMesh() const {
	return optimized_mesh_;
}

PointSet* PaintCanvas::pointSet() const {
	return point_set_;
}


void PaintCanvas::setShowInput(bool b) {
	show_input_ = b;
	update_all();
}


void PaintCanvas::setShowCandidates(bool b) {
	show_candidates_ = b;
	update_all();
}


void PaintCanvas::setShowResult(bool b) {
	show_result_ = b;
	update_all();
}


void PaintCanvas::refinePlanes() {
	if (!pointSet()) {
		Logger::warn("-") << "point set does not exist" << std::endl;
		return;
	}

	const std::vector<VertexGroup::Ptr>& groups = pointSet()->groups();
	if (groups.empty()) {
		Logger::warn("-") << "planar segments do not exist" << std::endl;
		return;
	}

	main_window_->disableActions(true);


	if (hypothesis_)
		delete hypothesis_;
	hypothesis_ = new HypothesisGenerator(point_set_);

	hypothesis_->refine_planes();

	main_window_->checkBoxShowInput->setChecked(true);
	main_window_->actionGenerateFacetHypothesis->setDisabled(false);
	main_window_->defaultRenderingForCandidates();

	hint_text_ = "Next: click \'Hypothesis\' to generate candidate faces.";
	hint_text2nd_ = "";

	update_all();
}


void PaintCanvas::generateFacetHypothesis() {
	if (!point_set_) {
		Logger::warn("-") << "point set does not exist" << std::endl;
		return;
	}

	if (!hypothesis_) {
		Logger::warn("-") << "please refine the planes first" << std::endl;
		return;
	}

	const std::vector<VertexGroup::Ptr>& groups = point_set_->groups();
	if (groups.empty()) {
		Logger::warn("-") << "planar segments do not exist" << std::endl;
		return;
	}

	main_window_->disableActions(true);

	Logger::out("-") << "generating plane hypothesis..." << std::endl;

	StopWatch w;
	hypothesis_mesh_ = hypothesis_->generate();
	if (hypothesis_mesh_) {
		Logger::out("-") << "done. " << w.elapsed() << " sec." << std::endl;

		MapFacetAttribute<Color> color(hypothesis_mesh_, "color");
		FOR_EACH_FACET(Map, hypothesis_mesh_, it)
			color[it] = random_color();

		main_window_->checkBoxShowInput->setChecked(false);
		main_window_->checkBoxShowCandidates->setChecked(true);
		main_window_->actionGenerateQualityMeasures->setDisabled(false);
		main_window_->defaultRenderingForCandidates();

		hint_text_ = "Next: click \'Confidences\' to compute point/face confidences.";
		hint_text2nd_ = "";

		update_all();
	}
	else {
		QMessageBox::warning(main_window_, "Error!", "Failed generating candidate faces. \nCheck if the input point cloud has good planar segments.");
		hint_text_ = "Failed generating candidate faces :-(";
		hint_text2nd_ = "Check if the input point cloud has good planar segments.";
	}
}


void PaintCanvas::generateQualityMeasures() {
	if (!point_set_) {
		Logger::warn("-") << "point set does not exist" << std::endl;
		return;
	}

	if (!hypothesis_mesh_ || !hypothesis_) {
		Logger::warn("-") << "face hypothesis do not exist" << std::endl;
		return;
	}

	const EdgeStyle s = mesh_render_->sharp_edge_style();
	if (s.visible) { // temporally disable rendering a large number of sharp edges
		EdgeStyle ss = s;
		ss.visible = false;
		mesh_render_->set_sharp_edge_style(ss);
	}

	main_window_->disableActions(true);
	
	hypothesis_->compute_confidences(hypothesis_mesh_, false);

	main_window_->checkBoxShowCandidates->setChecked(true);
	main_window_->actionOptimization->setDisabled(false);
	main_window_->defaultRenderingForCandidates();

	hint_text_ = "Next: click \'Optimization\' for face selection.";
	hint_text2nd_ = "";

	if (s.visible)  // restore
		mesh_render_->set_sharp_edge_style(s);

	update_all();
}


void PaintCanvas::optimization() {
	if (!point_set_) {
		Logger::warn("-") << "point set does not exist" << std::endl;
		return;
	}

	if (!hypothesis_mesh_ || !hypothesis_) {
		Logger::warn("-") << "face hypothesis do not exist" << std::endl;
		return;
	}

	const std::vector<VertexGroup::Ptr>& groups = point_set_->groups();
	if (groups.empty()) {
		Logger::warn("-") << "planar segments do not exist" << std::endl;
		return;
	}

	if (!hypothesis_->ready_for_optimization(hypothesis_mesh_)) {
		Logger::warn("-") << "please generate quality measures first" << std::endl;
		return;
	}

	main_window_->updateWeights();
	main_window_->disableActions(true);
	Map* mesh = Geom::duplicate(hypothesis_mesh_);

	FaceSelection selector(point_set_, mesh);
    selector.optimize(hypothesis_,
                      main_window_->active_solver(),
                      Method::weight_data_fitting,
                      Method::weight_model_coverage,
                      Method::weight_model_complexity);

#if 0 // not stable!!!
    { // to stitch the coincident edges and related vertices
		const HypothesisGenerator::Adjacency& adjacency = hypothesis_->extract_adjacency(mesh);
        MapEditor editor(mesh);
        for (auto pair : adjacency) {
            if (pair.size() != 2) {
                std::cerr << "error: an edge should be associated with two faces" << std::endl;
                continue;
            }
            auto h0 = pair[0];
            auto h1 = pair[1];
            if (dot(Geom::vector(h0), Geom::vector(h1)) < 0)
                editor.glue(h0->opposite(), h1->opposite());
        }

        int num = 0;
        FOR_EACH_EDGE(Map, mesh, it) {
            if (it->is_border_edge())
                ++num;
        }
        if (num == 0)
            Logger::out("-") << "final model is watertight" << std::endl;
        else
            Logger::warn("-") << "final model has " << num << " border edges" << std::endl;
    }
#endif

    optimized_mesh_ = mesh;

	main_window_->checkBoxShowInput->setChecked(false);
	main_window_->checkBoxShowCandidates->setChecked(false);
	main_window_->checkBoxShowResult->setChecked(true);
	main_window_->actionOptimization->setDisabled(false);
	main_window_->defaultRenderingForResult();

	hint_text_ = "Done! You may tune the parameters to reproduce the result.";
	hint_text2nd_ = "To see where the faces originate, check \'Per-face Color\' in the rendering panel.";

	update_all();
}
