#include "ControlPanelComponentV4.h"
#include "Theme.h"

ControlPanelComponentV4::ControlPanelComponentV4(IrisVSTV4AudioProcessor& p)
    : audioProcessor(p)
{
    // --- Row 1: + IR, Mix, Load Layout ---
    addAndMakeVisible(addIRButton);
    addIRButton.addListener(this);
    
    addAndMakeVisible(mixLabel);
    mixLabel.setText("Mix", juce::dontSendNotification);
    mixLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(mixSlider);
    mixSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    mixSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, "mix", mixSlider);
        
    addAndMakeVisible(loadLayoutButton);
    loadLayoutButton.addListener(this);
    
    addAndMakeVisible(broadcastButton);
    broadcastButton.addListener(this);
    
    // --- Row 2: + Wall, Wall Opacity, Save Layout ---
    addAndMakeVisible(addWallButton);
    addWallButton.addListener(this);
    
    addAndMakeVisible(wallOpacityLabel);
    wallOpacityLabel.setText("Wall Opacity", juce::dontSendNotification);
    wallOpacityLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(wallOpacitySlider);
    wallOpacitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    wallOpacitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    wallOpacityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, "wallOpacity", wallOpacitySlider);
        
    addAndMakeVisible(saveLayoutButton);
    saveLayoutButton.addListener(this);
    
    // --- Row 3: Freeze, Inertia, Spread ---
    addAndMakeVisible(freezeButton);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "freeze", freezeButton);
        
    addAndMakeVisible(inertiaLabel);
    inertiaLabel.setText("Inertia", juce::dontSendNotification);
    inertiaLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(inertiaSlider);
    inertiaSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    inertiaSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    inertiaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, "inertia", inertiaSlider);
        
    addAndMakeVisible(spreadLabel);
    spreadLabel.setText("Spread", juce::dontSendNotification);
    spreadLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(spreadSlider);
    spreadSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    spreadSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    spreadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, "spread", spreadSlider);
}

ControlPanelComponentV4::~ControlPanelComponentV4()
{
}

void ControlPanelComponentV4::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panelBackground); // Background
    
    // Header Style
    g.setColour(Theme::textSecondary);
    g.setFont(Theme::getHeadingFont(12.0f));
    
    // Group 1: Global
    g.drawText("GLOBAL", 10, 5, 200, 15, juce::Justification::left);
    
    int interactionHeaderY = (getHeight() * 0.66f) - 10;
    
    g.setColour(Theme::borderMinimal);
    g.fillRect(10, interactionHeaderY, getWidth()-20, 1);
    
    g.setColour(Theme::textSecondary);
    g.drawText("INTERACTION", 10, interactionHeaderY + 2, 200, 15, juce::Justification::left);
}

void ControlPanelComponentV4::resized()
{
    auto area = getLocalBounds().reduced(5);
    
    // Fixed height layout is safer for alignment
    int btnH = 28;
    int gap = 8;
    
    // --- GLOBAL ---
    area.removeFromTop(18); // Header space
    
    // Row 1
    auto row1 = area.removeFromTop(btnH);
    addIRButton.setBounds(row1.removeFromLeft(70));
    loadLayoutButton.setBounds(row1.removeFromRight(70));
    
    // Broadcast Button next to Load Layout? Or reduce Mix.
    // Let's put Broadcast before Load Layout (right side) or after +IR (left side).
    // User said "make mix slider smaller".
    // Let's use: [+IR][Mix.......][Broadcast][LoadLayout]
    
    broadcastButton.setBounds(row1.removeFromRight(80)); // 80px for menu
    
    auto mixArea = row1.reduced(10, 0);
    mixLabel.setBounds(mixArea.removeFromLeft(30));
    mixSlider.setBounds(mixArea);
    
    area.removeFromTop(gap);
    
    // Row 2
    auto row2 = area.removeFromTop(btnH);
    addWallButton.setBounds(row2.removeFromLeft(70)); 
    saveLayoutButton.setBounds(row2.removeFromRight(70));
    
    auto opArea = row2.reduced(10, 0);
    wallOpacityLabel.setBounds(opArea.removeFromLeft(70));
    wallOpacitySlider.setBounds(opArea);
    
    // --- INTERACTION ---
    // Push down to bottom 1/3 approx
    // Or just spacer
    // area.removeFromTop(30); 
    
    // Calculate exact Y for Interaction to match paint
    int interactionY = (getHeight() * 0.66f) + 10;
    
    // Reset area to that Y
    auto row3 = getLocalBounds().reduced(5);
    row3.setTop(interactionY);
    row3.setHeight(btnH);
    
    // Row 3: Freeze | Inertia | Spread
    int w = row3.getWidth() / 3;
    
    // Checkbox needs room for text "Freeze"
    auto r1 = row3.removeFromLeft(w);
    freezeButton.setBounds(r1.reduced(5, 0)); 
    // Ensure button has text
    // If it's a ToggleButton, text is right of box.
    
    auto r2 = row3.removeFromLeft(w);
    inertiaLabel.setBounds(r2.removeFromLeft(45));
    inertiaSlider.setBounds(r2);
    
    auto r3 = row3;
    spreadLabel.setBounds(r3.removeFromLeft(45));
    spreadSlider.setBounds(r3);
}

void ControlPanelComponentV4::buttonClicked(juce::Button* b)
{
    if (b == &addIRButton)
    {
        fileChooser = std::make_unique<juce::FileChooser>("Select IR File",
                                                          juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                                          "*.wav;*.aiff;*.mp3");
                                                          
        auto folderChooserFlags = juce::FileBrowserComponent::openMode | 
                                  juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::canSelectMultipleItems;

        fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& fc)
        {
            auto files = fc.getResults();
            for (auto& file : files)
            {
                if (file.existsAsFile())
                    audioProcessor.addIRFromFile(file);
            }
        });
    }
    else if (b == &addWallButton)
    {
        // Add random wall in center
        audioProcessor.addWall(0.4f, 0.4f, 0.6f, 0.6f);
    }
    else if (b == &loadLayoutButton)
    {
        fileChooser = std::make_unique<juce::FileChooser>("Load Layout",
                                                          juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                                          "*.json");
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc){
            auto f = fc.getResult();
            if (f.existsAsFile()) audioProcessor.loadLayoutFromJSON(f);
        });
    }
    else if (b == &saveLayoutButton)
    {
        fileChooser = std::make_unique<juce::FileChooser>("Save Layout",
                                                          juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                                          "*.json");
        auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc){
            auto f = fc.getResult();
            // Ensure extension
            if (!f.hasFileExtension("json")) f = f.withFileExtension("json");
            
            audioProcessor.saveLayoutToJSON(f);
        });
    }
    else if (b == &broadcastButton)
    {
        juce::PopupMenu m;
        m.addItem("Listener", true, audioProcessor.broadcastListener, [this](){ audioProcessor.broadcastListener = !audioProcessor.broadcastListener; });
        m.addItem("IRs", true, audioProcessor.broadcastIRs, [this](){ audioProcessor.broadcastIRs = !audioProcessor.broadcastIRs; });
        m.addItem("Walls", true, audioProcessor.broadcastWalls, [this](){ audioProcessor.broadcastWalls = !audioProcessor.broadcastWalls; });
        
        // Globals (Mix, Spread, etc.) - If we implement sync for them.
        m.addItem("Broadcasting", true, audioProcessor.broadcastGlobals, [this](){ audioProcessor.broadcastGlobals = !audioProcessor.broadcastGlobals; });
        
        // Settings Sync Helper (Force Full Sync)
        m.addSeparator();
        m.addItem("Force Send Full Sync", [this](){ audioProcessor.requestFullOSCSync(); });
        
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(broadcastButton));
    }
}

void ControlPanelComponentV4::update()
{
    // Attachments handle mostly everything. 
    // If we had manual updates, here they go.
}
