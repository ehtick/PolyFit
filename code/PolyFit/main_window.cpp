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

#include "main_window.h"

#include <QMessageBox>
#include <QFileDialog>
#include <QStatusBar>
#include <QSettings>
#include <QProgressBar>
#include <QComboBox>
#include <QMenu>
#include <QToolButton>

#include "paint_canvas.h"

#include "dlg/wgt_render.h"
#include "dlg/weight_panel_click.h"
#include "dlg/weight_panel_manual.h"

#include <basic/file_utils.h>
#include <model/map_attributes.h>
#include <model/map_builder.h>
#include <model/map_io.h>
#include <model/map_enumerator.h>
#include <model/point_set_io.h>
#include <method/method_global.h>
#include <basic/attribute_serializer.h>


MainWindow::MainWindow(QWidget *parent)
: QMainWindow(parent)
, curDataDirectory_(".")
{	
	setupUi(this);

	//////////////////////////////////////////////////////////////////////////

	Logger::initialize();
	Logger::instance()->register_client(this);
	Logger::instance()->set_value(Logger::LOG_REGISTER_FEATURES, "*"); // log everything
	Logger::instance()->set_value(Logger::LOG_FILE_NAME, "PolyFit.log");
	//Logger::instance()->set_value("log_features",
	//	"EigenSolver;MapBuilder;MapParameterizer\
	//	LinearSolver");

	// Liangliang: added the time stamp in the log file
	std::string tstr = String::from_current_time();
	Logger::out("") << "--- started at: " << tstr << " ---" << std::endl;
	
	Progress::instance()->set_client(this) ;

	AttributeSerializer::initialize();

	register_attribute_type<int>("int");
	register_attribute_type<float>("float");
	register_attribute_type<double>("double");
	register_attribute_type<bool>("bool") ;
	register_attribute_type<std::string>("string") ;
	register_attribute_type<vec2>("vec2") ;
	register_attribute_type<vec3>("vec3") ;
	register_attribute_type<vec4>("vec4") ;
	register_attribute_type<mat2>("mat2") ;
	register_attribute_type<mat3>("mat3") ;
	register_attribute_type<mat4>("mat4") ;
	register_attribute_type<Color>("Color");

	// ensure backward compatibility with .eobj files generated before.
	// PointXd/VectorXd do not exist anymore.
	register_attribute_type_alias("Vector2d", "vec2") ;
	register_attribute_type_alias("Vector3d", "vec3") ;
	register_attribute_type_alias("Point2d", "vec2") ;
	register_attribute_type_alias("Point3d", "vec3") ;

	//////////////////////////////////////////////////////////////////////////

	mainCanvas_ = new PaintCanvas(this);
	layoutCanvas->addWidget(mainCanvas_);

	//////////////////////////////////////////////////////////////////////////

	setWindowState(Qt::WindowMaximized);
	setFocusPolicy(Qt::StrongFocus);
	showMaximized();

	createRenderingPanel();

	createActions();
	createStatusBar();
	createToolBar();

	readSettings();
	setWindowTitle("PolyFit");

	setContextMenuPolicy(Qt::CustomContextMenu);
	setWindowIcon(QIcon(":/Resources/PolyFit.png"));

	setAcceptDrops(true);
	disableActions(true);
}


MainWindow::~MainWindow()
{
	if (wgtRender_)	delete wgtRender_;

	//////////////////////////////////////////////////////////////////////////

	AttributeSerializer::terminate();
	Progress::instance()->set_client(nil) ;
	Logger::instance()->unregister_client(this);
	Logger::terminate();
}


void MainWindow::out_message(const std::string& msg) {
	plainTextEditOutput->moveCursor(QTextCursor::End);
	plainTextEditOutput->insertPlainText(QString::fromStdString(msg));
	plainTextEditOutput->repaint();
	plainTextEditOutput->update();
}


void MainWindow::warn_message(const std::string& msg) {
	plainTextEditOutput->moveCursor(QTextCursor::End);
	plainTextEditOutput->insertPlainText(QString::fromStdString(msg));
	plainTextEditOutput->repaint();
	plainTextEditOutput->update();
}


void MainWindow::err_message(const std::string& msg) {
	plainTextEditOutput->moveCursor(QTextCursor::End);
	plainTextEditOutput->insertPlainText(QString::fromStdString(msg));
	plainTextEditOutput->repaint();
	plainTextEditOutput->update();
}


void MainWindow::status_message(const std::string& msg, int timeout) {
	statusBar()->showMessage(QString::fromStdString(msg), timeout);
}


void MainWindow::notify_progress(std::size_t value) {
	progress_bar_->setValue(value);
	progress_bar_->setTextVisible(value != 0);
	mainCanvas_->update_all();
}

void MainWindow::createActions() {
	connect(actionOpen, SIGNAL(triggered()), this, SLOT(open()));

	// ------------------------------------------------------

	// actions for save
	QAction* actionSaveReconstruction = new QAction(this);
	actionSaveReconstruction->setText("Save reconstruction");
	connect(actionSaveReconstruction, SIGNAL(triggered()), this, SLOT(saveReconstruction()));

	QAction* actionSaveCandidateFaces = new QAction(this);
	actionSaveCandidateFaces->setText("Save candidate faces");
	connect(actionSaveCandidateFaces, SIGNAL(triggered()), this, SLOT(saveCandidateFaces()));

	QMenu* saveMenu = new QMenu();
	saveMenu->addAction(actionSaveReconstruction);
	saveMenu->addSeparator();
	saveMenu->addAction(actionSaveCandidateFaces);

	QToolButton* saveToolButton = new QToolButton();
	saveToolButton->setText("Save");
	saveToolButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
	saveToolButton->setMenu(saveMenu);
	saveToolButton->setPopupMode(QToolButton::InstantPopup);
	QIcon saveIcon;
	saveIcon.addFile(QStringLiteral(":/Resources/filesave.png"), QSize(), QIcon::Normal, QIcon::Off);
	saveToolButton->setIcon(saveIcon);

	toolBarFile->insertWidget(actionSnapshot, saveToolButton);
	toolBarFile->insertSeparator(actionSnapshot);

	// ------------------------------------------------------

	connect(actionSnapshot, SIGNAL(triggered()), this, SLOT(snapshotScreen()));

	connect(actionRefinePlanes, SIGNAL(triggered()), mainCanvas_, SLOT(refinePlanes()));
	connect(actionGenerateFacetHypothesis, SIGNAL(triggered()), mainCanvas_, SLOT(generateFacetHypothesis()));
	connect(actionGenerateQualityMeasures, SIGNAL(triggered()), mainCanvas_, SLOT(generateQualityMeasures()));
	connect(actionOptimization, SIGNAL(triggered()), mainCanvas_, SLOT(optimization()));

	connect(checkBoxShowInput, SIGNAL(toggled(bool)), mainCanvas_, SLOT(setShowInput(bool)));
	connect(checkBoxShowCandidates, SIGNAL(toggled(bool)), mainCanvas_, SLOT(setShowCandidates(bool)));
	connect(checkBoxShowResult, SIGNAL(toggled(bool)), mainCanvas_, SLOT(setShowResult(bool)));

	wgtRender_ = new WgtRender(this);
	layoutRenderer->addWidget(wgtRender_);

	// about menu
	connect(actionAbout, SIGNAL(triggered()), this, SLOT(about()));
}


void MainWindow::createRenderingPanel() {
	default_fitting_ = truncate_digits(Method::weight_data_fitting, 3);
	default_coverage_ = truncate_digits(Method::weight_model_coverage, 3);
	default_complexity_ = truncate_digits(Method::weight_model_complexity, 3);

	panelClick_ = new WeightPanelClick(this);
    verticalLayoutWeights->addWidget(panelClick_);
	panelManual_ = nullptr;

	connect(pushButtonDefaultWeight, SIGNAL(pressed()), this, SLOT(resetWeights()));
	connect(checkBoxManualInputWeights, SIGNAL(toggled(bool)), this, SLOT(setManualInputWeights(bool)));
}


void MainWindow::updateWeights() {
    panelClick_->updateUI();
	if (panelManual_)
		panelManual_->updateWeights();
}


void MainWindow::disableActions(bool b) {
	actionRefinePlanes->setDisabled(b);
	actionGenerateFacetHypothesis->setDisabled(b);
	actionGenerateQualityMeasures->setDisabled(b);
	actionOptimization->setDisabled(b);
}


void MainWindow::defaultRenderingForCandidates() {
	wgtRender_->checkBoxPerFaceColor->setChecked(true);
}


void MainWindow::defaultRenderingForResult() {
	wgtRender_->checkBoxPerFaceColor->setChecked(false);
}


void MainWindow::resetWeights() {
	Method::weight_data_fitting = default_fitting_;
	Method::weight_model_coverage = default_coverage_;
	Method::weight_model_complexity = default_complexity_;

	panelClick_->updateUI();
	panelManual_->updateUI();
}


void MainWindow::setManualInputWeights(bool b) {
    if (!panelManual_) {
        panelManual_ = new WeightPanelManual(this);
        verticalLayoutWeights->addWidget(panelManual_);
        connect(panelClick_, SIGNAL(weights_changed()), panelManual_, SLOT(updateUI()));
    }

	if (b) {
		panelClick_->hide();
		panelManual_->show();
	}
	else {
		panelClick_->show();
		panelManual_->hide();
	}
}


void MainWindow::closeEvent(QCloseEvent *event)
{
	writeSettings();
	event->accept();
}

void MainWindow::createStatusBar()
{	
	statusLabel_ = new QLabel("Ready");
	statusLabel_->setFixedWidth(scrollArea->width());
	statusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	statusBar()->addWidget(statusLabel_, 1);

	QLabel* space1 = new QLabel;
	statusBar()->addWidget(space1, 1);

	int length = 200;
	numPointsLabel_ = new QLabel;
	numPointsLabel_->setFixedWidth(length);
	numPointsLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	statusBar()->addPermanentWidget(numPointsLabel_, 1);

	numHypoFacesLabel_ = new QLabel;
	numHypoFacesLabel_->setFixedWidth(length);
	numHypoFacesLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	statusBar()->addPermanentWidget(numHypoFacesLabel_, 1);

	numOptimizedFacesLabel_ = new QLabel;
	numOptimizedFacesLabel_->setFixedWidth(length);
	numOptimizedFacesLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	statusBar()->addPermanentWidget(numOptimizedFacesLabel_, 1);

	QLabel* space2 = new QLabel;
	statusBar()->addWidget(space2, 1);

	//////////////////////////////////////////////////////////////////////////

	progress_bar_ = new QProgressBar;
	progress_bar_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	progress_bar_->setFixedWidth(400);
	statusBar()->addPermanentWidget(progress_bar_, 1);

	//////////////////////////////////////////////////////////////////////////

	updateStatusBar();
}


void MainWindow::createToolBar()
{
	solverBox_ = new QComboBox(this);
	solverBox_->setFixedHeight(23);
	solverBox_->setEditable(false);
#ifdef HAS_GUROBI
    solverBox_->addItem("GUROBI");
#endif
    solverBox_->addItem("SCIP");
	solverBox_->addItem("GLPK");
	solverBox_->addItem("LPSOLVE");

	QLabel* label = new QLabel(this);
	label->setText("    Solver");
	label->setAlignment(Qt::AlignLeft);

	QVBoxLayout* layout = new QVBoxLayout;
	layout->addWidget(solverBox_);
	layout->addWidget(label);

	QWidget* widget = new QWidget(this);
	widget->setLayout(layout);

	toolBar->insertWidget(actionRefinePlanes, widget);

	toolBar->insertSeparator(actionRefinePlanes);
}


void MainWindow::updateStatusBar()
{
	QString points = "#points: 0";
	QString hypoFaces = "#faces(hypo): 0";
	QString optimizedFaces = "#faces(result): 0";

	if (mainCanvas_->pointSet()) {
		points = QString("#points: %1").arg(mainCanvas_->pointSet()->num_points());
	}
	if (mainCanvas_->hypothesisMesh()) {
		hypoFaces = QString("#faces(candidates): %1").arg(mainCanvas_->hypothesisMesh()->size_of_facets());
	}
	if (mainCanvas_->optimizedMesh()) {
		// I need to report the number of planar faces, instead of the original face candidates
		//optimizedFaces = QString("#faces(final): %1").arg(mainCanvas_->optimizedMesh()->size_of_facets());
		MapFacetAttribute<int> attrib(mainCanvas_->optimizedMesh());
		int num = MapEnumerator::enumerate_planar_components(mainCanvas_->optimizedMesh(), attrib);
		optimizedFaces = QString("#faces(final): %1").arg(num);
	}

	numPointsLabel_->setText(points);
	numHypoFacesLabel_->setText(hypoFaces);
	numOptimizedFacesLabel_->setText(optimizedFaces);
}


void MainWindow::about()
{
#if defined (ENV_32_BIT)
	QString title = QMessageBox::tr("<h3>PolyFit (32-bit)</h3>");
#elif defined (ENV_64_BIT)
	QString title = QMessageBox::tr("<h3>PolyFit (64-bit)</h3>");
#else
	QString title = QMessageBox::tr("<h3>PolyFit"</h3>);
#endif

#ifndef NDEBUG
	title += QMessageBox::tr(" (Debug Version)");
#endif

	QString text = QMessageBox::tr(
		"<p>PolyFit implements our <span style=\"font-style:italic;\">hypothesis and selection</span> based surface reconstruction method described in the following paper:</p>"
		
		"<p>--------------------------------------------------------------------------<br>"
		"<span style=\"font-style:italic;\">Liangliang Nan</span> and <span style=\"font-style:italic;\">Peter Wonka</span>.<br>"
		"<a href=\"https://3d.bk.tudelft.nl/liangliang/publications/2017/polyfit/polyfit.html\">PolyFit: Polygonal Surface Reconstruction from Point Clouds.</a><br>"
		"ICCV 2017.<br>"
		"--------------------------------------------------------------------------</p>"

		"<p>Extract planes? You can use my <a href=\"https://3d.bk.tudelft.nl/liangliang/software.html\">Mapple</a> program for plane extraction. Please refer to the ReadMe files for more details.</p>"

		"<p>For comments, suggestions, or any issues, please contact me at <a href=\"mailto:liangliang.nan@gmail.com\">liangliang.nan@gmail.com</a>.</p>"
		"<p>Liangliang Nan<br>"
		"<a href=\"https://3d.bk.tudelft.nl/liangliang/\">https://3d.bk.tudelft.nl/liangliang/</a><br>"
		"@July.18, 2017</p>"
	);

	QMessageBox::about(this, "About PolyFit", title + text);
}

void MainWindow::readSettings()
{
	QSettings settings("LiangliangNan", "PolyFit");
	curDataDirectory_ = settings.value("currentDirectory").toString();	
}

void MainWindow::writeSettings()
{
	QSettings settings("LiangliangNan", "PolyFit");
	settings.setValue("currentDirectory", curDataDirectory_);
}


void MainWindow::setCurrentFile(const QString &fileName)
{
	curDataDirectory_ = fileName.left(fileName.lastIndexOf("/") + 1); // path includes "/"

	setWindowModified(false);

	QString shownName = "Untitled";
	if (!fileName.isEmpty())
		shownName = strippedName(fileName);

	setWindowTitle(tr("%1[*] - %2").arg(shownName).arg(tr("PolyFit")));
}


bool MainWindow::open()
{
	QString fileName = QFileDialog::getOpenFileName(this,
		tr("Open file"), curDataDirectory_,
		tr("Supported Format (*.vg *.bvg *.obj)")
		);

	if (fileName.isEmpty())
		return false;

	return doOpen(fileName);
}


bool MainWindow::saveReconstruction()
{
	QString fileName = QFileDialog::getSaveFileName(this,
		tr("Save the reconstruction result into an OBJ file"), optimizedMeshFileName_,
		tr("Mesh (*.obj)")
		);

	if (fileName.isEmpty())
		return false;

	std::string ext = FileUtils::extension(fileName.toStdString());
	String::to_lowercase(ext);

	if (ext == "obj") {
		bool success = MapIO::save(fileName.toStdString(), canvas()->optimizedMesh());
		if (success) {
			setCurrentFile(fileName);
			status_message("File saved", 500);
			return true;
		}
	}

	status_message("Saving failed", 500);
	return false;
}


bool MainWindow::saveCandidateFaces()
{
	QString fileName = QFileDialog::getSaveFileName(this,
		tr("Save candidate faces into an OBJ file"), optimizedMeshFileName_,
		tr("Mesh (*.obj)")
	);

	if (fileName.isEmpty())
		return false;

	std::string ext = FileUtils::extension(fileName.toStdString());
	String::to_lowercase(ext);

	if (ext == "obj") {
		bool success = MapIO::save(fileName.toStdString(), canvas()->hypothesisMesh());
		if (success) {
			setCurrentFile(fileName);
			status_message("File saved", 500);
			return true;
		}
	}

	status_message("Saving failed", 500);
	return false;
}


bool MainWindow::doOpen(const QString &fileName)
{
	std::string name = fileName.toStdString();
	std::string ext = FileUtils::extension(name);
	String::to_lowercase(ext);

	Map* mesh = nil;
	PointSet* pset = nil;
	if (ext == "obj") {
		mesh = MapIO::read(name);
		if (mesh)
			optimizedMeshFileName_ = fileName;
	}
	else {
		pset = PointSetIO::read(name);
		if (pset)
			pointCloudFileName_ = fileName;
	}

	if (mesh)
		canvas()->setMesh(mesh);

	if (pset) {
		canvas()->clear();
		canvas()->setPointSet(pset);

		hypothesisMeshFileName_ = pointCloudFileName_;
		int idx = fileName.lastIndexOf(".");
		hypothesisMeshFileName_.truncate(idx);
		hypothesisMeshFileName_.append(".eobj");

		optimizedMeshFileName_ = pointCloudFileName_;
		idx = fileName.lastIndexOf(".");
		optimizedMeshFileName_.truncate(idx);
		optimizedMeshFileName_.append(".obj");
	}

	if (pset || mesh) {
		updateStatusBar();
		setCurrentFile(fileName);
		setWindowTitle(tr("%1[*] - %2").arg(strippedName(fileName)).arg(tr("PolyFit")));
		status_message("File loaded", 500);

		if (pset) {
			checkBoxShowInput->setChecked(true);
			checkBoxShowCandidates->setChecked(true);
			checkBoxShowResult->setChecked(true);
			actionRefinePlanes->setDisabled(false);
			actionGenerateFacetHypothesis->setDisabled(true);
			actionGenerateQualityMeasures->setDisabled(true);
			actionOptimization->setDisabled(true);
		}
		return true;
	} 	
	else {	
		status_message("Open failed", 500);
		return false;
	} 
}


QString MainWindow::strippedName(const QString &fullFileName)
{
	return QFileInfo(fullFileName).fileName();
}


void MainWindow::snapshotScreen() {
	const std::string& fileName = optimizedMeshFileName_.toStdString();
	const std::string& snapshot = FileUtils::replace_extension(fileName, "png");

	QString snapshotFileName = QFileDialog::getSaveFileName(this,
		tr("Save Snapshot"), QString::fromStdString(snapshot),
		tr("PNG Image (*.png)")
	);

	canvas()->snapshotScreen(snapshotFileName);
}


LinearProgramSolver::SolverName MainWindow::active_solver() const {
	const QString& solverString = solverBox_->currentText();

	//if (solverString == "GLPK")
	//	return LinearProgramSolver::GLPK;
#ifdef HAS_GUROBI
	if (solverString == "GUROBI")
		return LinearProgramSolver::GUROBI;
#endif
	//else if (solverString == "LPSOLVE")
	//	return LinearProgramSolver::LPSOLVE;
	
    // default to SCIP
	return LinearProgramSolver::SCIP;
}
