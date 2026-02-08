#include "ControlPanelComponentV3.h"

ControlPanelComponentV3::ControlPanelComponentV3(IrisVSTV3AudioProcessor& p)
    : audioProcessor(p)
{
    addAndMakeVisible(addIRButton);
    addIRButton.addListener(this); 
    
    addAndMakeVisible(addWallButton);
    addWallButton.addListener(this); 

    // Inertia
    addAndMakeVisible(inertiaLabel);
    inertiaLabel.setText("Inertia", juce::dontSendNotification);
    inertiaLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(inertiaMinusLabel);
    inertiaMinusLabel.setText("-", juce::dontSendNotification);
    inertiaMinusLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(inertiaPlusLabel);
    inertiaPlusLabel.setText("+", juce::dontSendNotification);
    inertiaPlusLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(inertiaSlider);
    inertiaSlider.setRange(0.0, 1.0);
    inertiaSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    inertiaSlider.onValueChange = [this] { *audioProcessor.inertiaParam = (float)inertiaSlider.getValue(); };
    inertiaSlider.setValue(*audioProcessor.inertiaParam, juce::dontSendNotification);
    
    // Freeze
    addAndMakeVisible(freezeLabel);
    freezeLabel.setText("FREEZE", juce::dontSendNotification);
    freezeLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(freezeButton);
    freezeButton.setButtonText("Freeze");
    freezeButton.setClickingTogglesState(true);
    freezeButton.onClick = [this] { *audioProcessor.freezeParam = freezeButton.getToggleState(); };
    freezeButton.setToggleState(*audioProcessor.freezeParam, juce::dontSendNotification);
    
    // Load Layout
    addAndMakeVisible(loadLayoutButton);
    loadLayoutButton.onClick = [this] {
        fileChooser = std::make_unique<juce::FileChooser>("Select Layout JSON",
                                                  juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                                  "*.json");
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc){
             auto f = fc.getResult();
             if (f.existsAsFile()) audioProcessor.loadLayoutFromJSON(f);
        });
    };
    
    // Spread
    addAndMakeVisible(spreadLabel);
    spreadLabel.setText("Spatial Spread", juce::dontSendNotification);
    spreadLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(spreadMinusLabel);
    spreadMinusLabel.setText("-", juce::dontSendNotification);
    spreadMinusLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(spreadPlusLabel);
    spreadPlusLabel.setText("+", juce::dontSendNotification);
    spreadPlusLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(spreadSlider);
    spreadSlider.setRange(0.0, 1.0);
    spreadSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    spreadSlider.onValueChange = [this] { *audioProcessor.spreadParam = (float)spreadSlider.getValue(); };
    spreadSlider.setValue(*audioProcessor.spreadParam, juce::dontSendNotification);
    
    // Mix
    addAndMakeVisible(dryLabel);
    dryLabel.setText("Dry", juce::dontSendNotification);
    dryLabel.setJustificationType(juce::Justification::centredRight);
    
    addAndMakeVisible(wetLabel);
    wetLabel.setText("Wet", juce::dontSendNotification);
    wetLabel.setJustificationType(juce::Justification::centredLeft);
    
    addAndMakeVisible(mixLabel); // Not used in new design effectively, or maybe removed? Kept for safety but hidden?
    // User requested "Dry" on dry side and "Wet" on wet side. 
    // Mix label was "Mix".
    
    addAndMakeVisible(mixSlider);
    mixSlider.setRange(0.0, 1.0);
    mixSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mixSlider.onValueChange = [this] { *audioProcessor.mixParam = (float)mixSlider.getValue(); };
    mixSlider.setValue(*audioProcessor.mixParam, juce::dontSendNotification);

    // Wall Opacity
    addAndMakeVisible(wallOpacityLabel);
    wallOpacityLabel.setText("Wall Opacity", juce::dontSendNotification);
    wallOpacityLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(wallOpacitySlider);
    wallOpacitySlider.setRange(0.0, 1.0);
    wallOpacitySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    wallOpacitySlider.onValueChange = [this] { *audioProcessor.wallOpacityParam = (float)wallOpacitySlider.getValue(); };
    wallOpacitySlider.setValue(*audioProcessor.wallOpacityParam, juce::dontSendNotification);
    
    update(); 
}

ControlPanelComponentV3::~ControlPanelComponentV3()
{
    addIRButton.removeListener(this);
    addWallButton.removeListener(this);
}

void ControlPanelComponentV3::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void ControlPanelComponentV3::resized()
{
    auto bound = getLocalBounds().reduced(5);
    
    // Line 1:  + IR | Dry | Slider | Wet | Load Layout
    auto line1 = bound.removeFromTop(40);
    
    juce::FlexBox fb1;
    fb1.flexDirection = juce::FlexBox::Direction::row;
    fb1.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
    fb1.alignItems = juce::FlexBox::AlignItems::center;
    
    // + IR
    fb1.items.add(juce::FlexItem(addIRButton).withWidth(80).withHeight(30));
    
    // + Wall
    fb1.items.add(juce::FlexItem(addWallButton).withWidth(80).withHeight(30).withMargin(juce::FlexItem::Margin(0,0,0,10)));
    
    // Mix Group (Center)
    // Create a sub-flex for mix
    juce::FlexBox mixBox;
    mixBox.flexDirection = juce::FlexBox::Direction::row;
    mixBox.alignItems = juce::FlexBox::AlignItems::center;
    
    mixBox.items.add(juce::FlexItem(dryLabel).withWidth(40).withHeight(30));
    mixBox.items.add(juce::FlexItem(mixSlider).withWidth(150).withHeight(30));
    mixBox.items.add(juce::FlexItem(wetLabel).withWidth(40).withHeight(30));
    
    // Add MixBox as item in fb1? 
    // Juce FlexItem can contain a Component. We can't easily nest FlexBox unless we wrap in Component.
    // Instead, just add items directly to fb1 with spacers.
    
    // Actually, spacers are better.
    
    fb1.items.add(juce::FlexItem(dryLabel).withWidth(40).withHeight(30).withMargin(juce::FlexItem::Margin(0,0,0,20)));
    fb1.items.add(juce::FlexItem(mixSlider).withWidth(150).withHeight(30));
    fb1.items.add(juce::FlexItem(wetLabel).withWidth(40).withHeight(30));
    
    // Load Layout
    fb1.items.add(juce::FlexItem(loadLayoutButton).withWidth(100).withHeight(30).withMargin(juce::FlexItem::Margin(0,0,0,20)));
    
    fb1.performLayout(line1);
    
    // Line 2: Freeze | Inertia | Spread
    auto line2 = bound.removeFromTop(60);
    line2.removeFromTop(5); // Spacer
    
    juce::FlexBox fb2;
    fb2.flexDirection = juce::FlexBox::Direction::row;
    fb2.justifyContent = juce::FlexBox::JustifyContent::flexStart; // Or spaceAround
    fb2.alignItems = juce::FlexBox::AlignItems::center;
    
    // Group 1: Freeze
    // We need a vertical layout for this item.
    // Since we can't nest flex w/o component, let's manually place them or assume fixed/relative widths
    // Re-thinking: Using FlexBox for the whole row might be tricky if we want columns.
    // Let's divide line2 into 3 equal chunks or specific widths.
    
    int w = line2.getWidth();
    int sectionW = w / 3;
    
    auto section1 = line2.removeFromLeft(100); // Freeze
    auto section2 = line2.removeFromLeft(200); // Inertia
    auto section3 = line2.removeFromLeft(200); // Spread
    
    // Freeze Section
    {
       juce::FlexBox fb;
       fb.flexDirection = juce::FlexBox::Direction::column;
       fb.justifyContent = juce::FlexBox::JustifyContent::center;
       fb.alignItems = juce::FlexBox::AlignItems::center;
       fb.items.add(juce::FlexItem(freezeLabel).withWidth(80).withHeight(20));
       fb.items.add(juce::FlexItem(freezeButton).withWidth(80).withHeight(25));
       fb.performLayout(section1); 
    }
    
    // Inertia Section
    {
       juce::FlexBox fb;
       fb.flexDirection = juce::FlexBox::Direction::column;
       fb.justifyContent = juce::FlexBox::JustifyContent::center;
       fb.alignItems = juce::FlexBox::AlignItems::center;
       
       fb.items.add(juce::FlexItem(inertiaLabel).withWidth(100).withHeight(20));
       
       // Inner Row
       // Check: Can we do this without a component?
       // Just manual placement relative to section2 bounds?
       // Or simpler:
       // Top half: Label
       // Bottom half: - Slider +
       
       auto r = section2;
       auto rTop = r.removeFromTop(r.getHeight()/2);
       inertiaLabel.setBounds(rTop);
       
       // Bottom Row
       juce::FlexBox fbRow;
       fbRow.flexDirection = juce::FlexBox::Direction::row;
       fbRow.justifyContent = juce::FlexBox::JustifyContent::center;
       fbRow.alignItems = juce::FlexBox::AlignItems::center;
       fbRow.items.add(juce::FlexItem(inertiaMinusLabel).withWidth(20).withHeight(20));
       fbRow.items.add(juce::FlexItem(inertiaSlider).withWidth(100).withHeight(25));
       fbRow.items.add(juce::FlexItem(inertiaPlusLabel).withWidth(20).withHeight(20));
       fbRow.performLayout(r);
    }
    
    // Spread Section
    {
       auto r = section3;
       auto rTop = r.removeFromTop(r.getHeight()/2);
       spreadLabel.setBounds(rTop);
       
       juce::FlexBox fbRow;
       fbRow.flexDirection = juce::FlexBox::Direction::row;
       fbRow.justifyContent = juce::FlexBox::JustifyContent::center;
       fbRow.alignItems = juce::FlexBox::AlignItems::center;
       fbRow.items.add(juce::FlexItem(spreadMinusLabel).withWidth(20).withHeight(20));
       fbRow.items.add(juce::FlexItem(spreadSlider).withWidth(100).withHeight(25));
       fbRow.items.add(juce::FlexItem(spreadPlusLabel).withWidth(20).withHeight(20));
       fbRow.performLayout(r);
    }
    
    // Line 3: Wall Opacity
    auto line3 = bound.removeFromTop(40);
    line3.removeFromTop(5);
    
    juce::FlexBox fb3;
    fb3.flexDirection = juce::FlexBox::Direction::row;
    fb3.justifyContent = juce::FlexBox::JustifyContent::center;
    fb3.alignItems = juce::FlexBox::AlignItems::center;
    
    fb3.items.add(juce::FlexItem(wallOpacityLabel).withWidth(80).withHeight(20));
    fb3.items.add(juce::FlexItem(wallOpacitySlider).withWidth(100).withHeight(25));
    
    fb3.performLayout(line3);
}

void ControlPanelComponentV3::buttonClicked (juce::Button* button)
{
    if (button == &addIRButton)
    {
        fileChooser = std::make_unique<juce::FileChooser>("Select IR File",
                                                          juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                                          "*.wav;*.aiff");

        auto folderChooserFlags = juce::FileBrowserComponent::openMode | 
                                  juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                audioProcessor.addIRFromFile(file);
            }
        });
    }
    else if (button == &addWallButton)
    {
        // Add default wall (horizontal near center)
        audioProcessor.addWall(0.3f, 0.5f, 0.7f, 0.5f);
    }
}

void ControlPanelComponentV3::update()
{
    // No local property updates (handled by WallList)
}


