#include "ControlPanelComponent.h"
#include "Theme.h"

ControlPanelComponent::ControlPanelComponent(IrisAudioProcessor& p)
    : audioProcessor(p)
{
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

    addAndMakeVisible(freezeButton);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "freeze", freezeButton);

    addAndMakeVisible(normalizeButton);
    normalizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "normalize", normalizeButton);

    addAndMakeVisible(alignButton);
    alignAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "align", alignButton);

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

    addAndMakeVisible(outputGainLabel);
    outputGainLabel.setText("Output", juce::dontSendNotification);
    outputGainLabel.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(outputGainSlider);
    outputGainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    outputGainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    outputGainSlider.setTextValueSuffix(" dB");
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, "outputGain", outputGainSlider);

    addAndMakeVisible(midiButton);
    midiButton.setTooltip("MIDI input and MIDI learn mappings. Right-click any slider or toggle to MIDI-learn it.");
    midiButton.addListener(this);

    makeLearnable(mixSlider,         "mix");
    makeLearnable(wallOpacitySlider, "wallOpacity");
    makeLearnable(outputGainSlider,  "outputGain");
    makeLearnable(inertiaSlider,     "inertia");
    makeLearnable(spreadSlider,      "spread");
    makeLearnable(freezeButton,      "freeze");
    makeLearnable(normalizeButton,   "normalize");
    makeLearnable(alignButton,       "align");
}

ControlPanelComponent::~ControlPanelComponent() {}

template <class Control>
void ControlPanelComponent::makeLearnable(Control& c, const juce::String& paramId)
{
    c.paramId = paramId;
    c.onMidiMenu = [this, &c, paramId] { MidiLearnUI::showMenu(audioProcessor, paramId, c); };
    learnables.push_back({ &c, paramId });
}

void ControlPanelComponent::paintOverChildren(juce::Graphics& g)
{
    for (auto& [comp, paramId] : learnables)
        MidiLearnUI::paintState(g, audioProcessor, paramId, comp->getBounds());

    const auto status = audioProcessor.midiLearn.getStatus();
    if (status.isNotEmpty())
    {
        g.setFont(Theme::getBaseFont(10.0f));
        g.setColour(audioProcessor.midiLearn.getLearningParam().isNotEmpty() ? juce::Colours::orange
                                                                               : Theme::textSecondary);
        g.drawText(status, getWidth() / 3, 4, getWidth() * 2 / 3 - 10, 14, juce::Justification::right, true);
    }
}

void ControlPanelComponent::showMidiSetupMenu()
{
    auto& ml = audioProcessor.midiLearn;
    juce::PopupMenu m;

    m.addSectionHeader("MIDI input");
    const auto selection = ml.getDeviceSelection();
    m.addItem("All MIDI inputs", true, selection == "*", [&ml] { ml.setDeviceSelection("*"); });
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        m.addItem(d.name, true, selection == d.identifier, [&ml, id = d.identifier] { ml.setDeviceSelection(id); });
    m.addItem("No MIDI devices", true, selection.isEmpty(), [&ml] { ml.setDeviceSelection({}); });
    m.addSeparator();
    m.addItem("Also use MIDI sent by the host", true, ml.isHostMidiEnabled(),
              [&ml] { ml.setHostMidiEnabled(! ml.isHostMidiEnabled()); });

    m.addSectionHeader("Mappings (right-click a control to learn)");
    const auto mappings = ml.getAllMappings();
    if (mappings.empty())
        m.addItem("No mappings yet", false, false, nullptr);

    for (const auto& [id, mapping] : mappings)
    {
        auto* param = audioProcessor.parameters.getParameter(id);
        m.addSubMenu((param != nullptr ? param->getName(32) : id) + "   " + IrisMidiLearn::describe(mapping),
                     MidiLearnUI::buildMenu(audioProcessor, id));
    }

    m.addSeparator();
    if (ml.getLearningParam().isNotEmpty())
        m.addItem("Cancel MIDI Learn", [&ml] { ml.cancelLearning(); });
    m.addItem("Clear all MIDI mappings", ! mappings.empty(), false, [&ml] { ml.clearAll(); });

    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(midiButton));
}

void ControlPanelComponent::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panelBackground);

    g.setColour(Theme::textSecondary);
    g.setFont(Theme::getHeadingFont(12.0f));
    g.drawText("GLOBAL", 10, 5, 200, 15, juce::Justification::left);

    const int dividerY = 130;
    g.setColour(Theme::borderMinimal);
    g.fillRect(10, dividerY, getWidth() - 20, 1);

    g.setColour(Theme::textSecondary);
    g.drawText("INTERACTION", 10, dividerY + 2, 200, 15, juce::Justification::left);
}

void ControlPanelComponent::resized()
{
    auto area = getLocalBounds().reduced(5);
    const int btnH = 28;
    const int gap  = 8;

    area.removeFromTop(18);

    auto row1 = area.removeFromTop(btnH);
    addIRButton.setBounds(row1.removeFromLeft(70));
    loadLayoutButton.setBounds(row1.removeFromRight(70));
    broadcastButton.setBounds(row1.removeFromRight(80));
    midiButton.setBounds(row1.removeFromRight(46).reduced(2, 0));
    auto mixArea = row1.reduced(10, 0);
    mixLabel.setBounds(mixArea.removeFromLeft(30));
    mixSlider.setBounds(mixArea);

    area.removeFromTop(gap);

    auto row2 = area.removeFromTop(btnH);
    addWallButton.setBounds(row2.removeFromLeft(70));
    saveLayoutButton.setBounds(row2.removeFromRight(70));
    auto opArea = row2.reduced(10, 0);
    wallOpacityLabel.setBounds(opArea.removeFromLeft(70));
    wallOpacitySlider.setBounds(opArea);

    area.removeFromTop(gap);

    auto row2b = area.removeFromTop(btnH);
    normalizeButton.setBounds(row2b.removeFromLeft(120).reduced(5, 0));
    alignButton.setBounds(row2b.removeFromLeft(120).reduced(5, 0));
    auto gainArea = row2b;
    outputGainLabel.setBounds(gainArea.removeFromLeft(45));
    outputGainSlider.setBounds(gainArea);

    // Interaction section starts at a fixed Y so it stays aligned with the divider drawn in paint().
    const int interactionY = 145;
    auto row4 = getLocalBounds().reduced(5);
    row4.setTop(interactionY);
    row4.setHeight(btnH);

    int colW = row4.getWidth() / 3;
    freezeButton.setBounds(row4.removeFromLeft(colW).reduced(5, 0));
    auto inertiaArea = row4.removeFromLeft(colW);
    inertiaLabel.setBounds(inertiaArea.removeFromLeft(45));
    inertiaSlider.setBounds(inertiaArea);
    spreadLabel.setBounds(row4.removeFromLeft(45));
    spreadSlider.setBounds(row4);
}

void ControlPanelComponent::buttonClicked(juce::Button* b)
{
    if (b == &addIRButton)
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select IR File",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*.wav;*.WAV;*.aif;*.aiff;*.flac");

        auto flags = juce::FileBrowserComponent::openMode
                   | juce::FileBrowserComponent::canSelectFiles
                   | juce::FileBrowserComponent::canSelectMultipleItems;

        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
        {
            for (auto& file : fc.getResults())
                if (file.existsAsFile())
                    audioProcessor.addIRFromFile(file);
        });
    }
    else if (b == &addWallButton)
    {
        audioProcessor.addWall(0.4f, 0.4f, 0.6f, 0.6f);
    }
    else if (b == &loadLayoutButton)
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Load Layout",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*.json");

        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.existsAsFile()) audioProcessor.loadLayoutFromJSON(f);
        });
    }
    else if (b == &saveLayoutButton)
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Save Layout",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*.json");

        auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;   // dialog cancelled
            if (!f.hasFileExtension("json")) f = f.withFileExtension("json");
            audioProcessor.saveLayoutToJSON(f);
        });
    }
    else if (b == &midiButton)
    {
        showMidiSetupMenu();
    }
    else if (b == &broadcastButton)
    {
        juce::PopupMenu m;
        m.addItem("Listener",     true, audioProcessor.broadcastListener, [this]() { audioProcessor.broadcastListener = !audioProcessor.broadcastListener; });
        m.addItem("IRs",          true, audioProcessor.broadcastIRs,      [this]() { audioProcessor.broadcastIRs      = !audioProcessor.broadcastIRs;      });
        m.addItem("Walls",        true, audioProcessor.broadcastWalls,    [this]() { audioProcessor.broadcastWalls    = !audioProcessor.broadcastWalls;    });
        m.addSeparator();
        m.addItem("Inertia",      true, audioProcessor.broadcastInertia,     [this]() { audioProcessor.broadcastInertia     = !audioProcessor.broadcastInertia;     });
        m.addItem("Freeze",       true, audioProcessor.broadcastFreeze,      [this]() { audioProcessor.broadcastFreeze      = !audioProcessor.broadcastFreeze;      });
        m.addItem("Spread",       true, audioProcessor.broadcastSpread,      [this]() { audioProcessor.broadcastSpread      = !audioProcessor.broadcastSpread;      });
        m.addItem("Mix",          true, audioProcessor.broadcastMix,         [this]() { audioProcessor.broadcastMix         = !audioProcessor.broadcastMix;         });
        m.addItem("Wall Opacity", true, audioProcessor.broadcastWallOpacity, [this]() { audioProcessor.broadcastWallOpacity = !audioProcessor.broadcastWallOpacity; });
        m.addItem("Normalize",    true, audioProcessor.broadcastNormalize,   [this]() { audioProcessor.broadcastNormalize   = !audioProcessor.broadcastNormalize;   });
        m.addItem("Align",        true, audioProcessor.broadcastAlign,       [this]() { audioProcessor.broadcastAlign       = !audioProcessor.broadcastAlign;       });
        m.addSeparator();
        m.addItem("Force Send Full Sync", [this]() { audioProcessor.requestFullOSCSync(); });
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(broadcastButton));
    }
}

void ControlPanelComponent::update() {}
