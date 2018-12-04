#pragma once

#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/TextureFont.h"
#include "cinder/gl/gl.h"

#include "peprimgui.h"

#include "ThreadPool.h"

#include "ModelView.h"
#include "ProgressIndicator.h"
#include "SidePane.h"
#include "Toolbar.h"
#include "commands/CommandManager.h"

using namespace ci;
using namespace ci::app;
using namespace std;

namespace pepr3d {

class ITool;
class Geometry;

class MainApplication : public App {
   public:
    void setup() override;
    void update() override;
    void draw() override;
    void resize() override;
    void mouseDown(MouseEvent event) override;
    void mouseDrag(MouseEvent event) override;
    void mouseUp(MouseEvent event) override;
    void mouseWheel(MouseEvent event) override;
    void mouseMove(MouseEvent event) override;
    void fileDrop(FileDropEvent event) override;

    MainApplication();

    static void prepareSettings(Settings* settings) {
        assert(settings != nullptr);
#if defined(CINDER_MSW_DESKTOP)
        settings->setConsoleWindowEnabled(true);
#endif
    }

    Toolbar& getToolbar() {
        return mToolbar;
    }

    SidePane& getSidePane() {
        return mSidePane;
    }

    ModelView& getModelView() {
        return mModelView;
    }

    bool isDemoWindowShown() {
        return mShowDemoWindow;
    }

    void showDemoWindow(bool show = true) {
        mShowDemoWindow = show;
    }

    void openFile(const std::string& path);
    void saveFile(const std::string& filePath, const std::string& fileName, const std::string& fileType);

    using ToolsVector = std::vector<std::unique_ptr<ITool>>;

    ToolsVector::iterator getToolsBegin() {
        return mTools.begin();
    }

    ToolsVector::iterator getToolsEnd() {
        return mTools.end();
    }

    ToolsVector::iterator getCurrentToolIterator() {
        assert(mTools.size() > 0);
        assert(mCurrentToolIterator != mTools.end());
        return mCurrentToolIterator;
    }

    ITool* getCurrentTool() {
        if(mTools.size() < 1 || mCurrentToolIterator == mTools.end()) {
            return nullptr;
        }
        return (*mCurrentToolIterator).get();
    }

    void setCurrentToolIterator(ToolsVector::iterator tool) {
        assert(mTools.size() > 0);
        assert(tool != mTools.end());
        assert((*tool)->isEnabled());
        (*mCurrentToolIterator)->onToolDeselect(mModelView);
        mCurrentToolIterator = tool;
    }

    Geometry* getCurrentGeometry() {
        return mGeometry.get();
    }

    CommandManager<Geometry>* getCommandManager() {
        return mCommandManager.get();
    }

    void showImportDialog();

    void showExportDialog() {
        mShowExportDialog = true;
    }

    void setError(const std::string& caption, const std::string& description = "") {
        mErrorCaption = caption;
        mErrorDescription = description;
    }

    void drawTooltipOnHover(const std::string& label, const std::string& shortcut = "",
                            const std::string& description = "", const std::string& disabled = "",
                            glm::vec2 position = glm::vec2(-1.0f), glm::vec2 pivot = glm::vec2(0.0f));

   private:
    void setupIcon();
    void drawExportDialog();
    void drawErrorDialog();
    void willResignActive();
    void didBecomeActive();
    bool isWindowObscured();

    bool mShouldSkipDraw = false;
    bool mIsFocused = true;

    Toolbar mToolbar;
    SidePane mSidePane;
    ModelView mModelView;
    ProgressIndicator mProgressIndicator;
    bool mShowDemoWindow = false;
    bool mShowExportDialog = false;
    bool mShouldExportInNewFolder = false;

    std::string mErrorCaption;
    std::string mErrorDescription;
    void resetError() {
        mErrorCaption = "";
        mErrorDescription = "";
    }

    ToolsVector mTools;
    ToolsVector::iterator mCurrentToolIterator;

    std::shared_ptr<Geometry> mGeometry;
    std::shared_ptr<Geometry>
        mGeometryInProgress;  // used for async loading of Geometry, is nullptr if nothing is being loaded
    std::unique_ptr<CommandManager<Geometry>> mCommandManager;

    std::string mGeometryFileName;

    ::ThreadPool mThreadPool;
};

}  // namespace pepr3d
