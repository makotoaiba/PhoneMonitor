#include "PluginEditor.h"
#include "qrcodegen/qrcodegen.hpp"

namespace
{
const juce::Colour cBg     (0xff1b2430);
const juce::Colour cPanel  (0xff243042);
const juce::Colour cLine   (0xff34425a);
const juce::Colour cText   (0xffe8ecf2);
const juce::Colour cMuted  (0xff93a1b5);
const juce::Colour cAmber  (0xfff2b544);
const juce::Colour cGreen  (0xff6fcf97);
const juce::Colour cRed    (0xffeb5757);

juce::String jp (const char* s) { return juce::String::fromUTF8 (s); }

int addressRank (const juce::String& a)
{
    if (a.startsWith ("192.168.")) return 0;
    if (a.startsWith ("10."))      return 1;
    if (a.startsWith ("172."))
    {
        const int second = a.fromFirstOccurrenceOf (".", false, false).getIntValue();
        if (second >= 16 && second <= 31) return 2;
    }
    return 3;
}
} // namespace

//==============================================================================
void QrView::setText (const juce::String& t)
{
    if (t == text)
        return;
    text = t;
    modules.clear();
    if (text.isNotEmpty())
    {
        try
        {
            const auto qr = qrcodegen::QrCode::encodeText (text.toRawUTF8(), qrcodegen::QrCode::Ecc::MEDIUM);
            const int n = qr.getSize();
            modules.assign ((size_t) n, std::vector<bool> ((size_t) n));
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                    modules[(size_t) y][(size_t) x] = qr.getModule (x, y);
        }
        catch (...) { modules.clear(); }
    }
    repaint();
}

void QrView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (modules.empty())
    {
        g.setColour (cLine);
        g.drawRoundedRectangle (b.reduced (1.0f), 8.0f, 1.0f);
        return;
    }
    g.setColour (juce::Colours::white);
    g.fillRoundedRectangle (b, 8.0f);
    const int n = (int) modules.size();
    const float cell = juce::jmin (b.getWidth(), b.getHeight()) / (float) (n + 4);
    const float ox = b.getX() + (b.getWidth()  - cell * (float) n) * 0.5f;
    const float oy = b.getY() + (b.getHeight() - cell * (float) n) * 0.5f;
    g.setColour (juce::Colours::black);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (modules[(size_t) y][(size_t) x])
                g.fillRect (ox + (float) x * cell, oy + (float) y * cell, cell + 0.5f, cell + 0.5f);
}

//==============================================================================
PhoneMonitorEditor::LnF::LnF()
{
    setDefaultSansSerifTypefaceName ("Yu Gothic UI");
    setColour (juce::TextButton::buttonColourId, cLine);
    setColour (juce::TextButton::textColourOffId, cText);
    setColour (juce::TextButton::buttonOnColourId, cAmber);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff2a1d00));
    setColour (juce::ToggleButton::textColourId, cText);
    setColour (juce::ToggleButton::tickColourId, cAmber);
    setColour (juce::ToggleButton::tickDisabledColourId, cMuted);
    setColour (juce::TextEditor::backgroundColourId, cBg);
    setColour (juce::TextEditor::textColourId, cText);
    setColour (juce::TextEditor::outlineColourId, cLine);
    setColour (juce::TextEditor::focusedOutlineColourId, cAmber);
    setColour (juce::CaretComponent::caretColourId, cAmber);
}

void PhoneMonitorEditor::styleLabel (juce::Label& l, float size, juce::Colour c, bool bold)
{
    l.setFont (juce::FontOptions ("Yu Gothic UI", size, bold ? juce::Font::bold : juce::Font::plain));
    l.setColour (juce::Label::textColourId, c);
    l.setJustificationType (juce::Justification::topLeft);
    l.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (l);
}

PhoneMonitorEditor::PhoneMonitorEditor (PhoneMonitorProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    sendToggle.setButtonText (jp (u8"スマホへ送信"));
    sendToggle.setToggleState (proc.sendEnabled.load(), juce::dontSendNotification);
    sendToggle.onClick = [this] { proc.sendEnabled = sendToggle.getToggleState(); };
    addAndMakeVisible (sendToggle);

    // 同じWi-Fi
    styleLabel (lanTitle, 16.0f, cText, true);
    lanTitle.setText (jp (u8"同じWi-Fiで聴く"), juce::dontSendNotification);
    styleLabel (lanUrlLabel, 14.0f, cAmber);
    styleLabel (lanInfo, 12.5f, cMuted);
    addAndMakeVisible (lanQr);

    copyLanBtn.setButtonText (jp (u8"URLをコピー"));
    copyLanBtn.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (lanUrl()); };
    addAndMakeVisible (copyLanBtn);

    switchAddrBtn.setButtonText (jp (u8"アドレス切替"));
    switchAddrBtn.onClick = [this]
    {
        refreshAddresses();
        if (addresses.size() > 1)
        {
            addressIndex = (addressIndex + 1) % addresses.size();
            proc.setPreferredAddress (addresses[addressIndex]);
        }
    };
    addAndMakeVisible (switchAddrBtn);

    // 外部公開
    styleLabel (remoteTitle, 16.0f, cText, true);
    remoteTitle.setText (jp (u8"遠くの人に聴かせる"), juce::dontSendNotification);

    remoteBtn.onClick = [this]
    {
        if (proc.isRemoteActive())
            proc.stopRemote();
        else
        {
            proc.setAllowedMails (mailEditor.getText().trim());
            proc.startRemote();
        }
    };
    addAndMakeVisible (remoteBtn);

    styleLabel (mailLabel, 12.5f, cMuted);
    mailLabel.setText (jp (u8"メール認証（任意）：聴ける人のアドレス"), juce::dontSendNotification);
    mailEditor.setText (proc.getAllowedMails(), false);
    mailEditor.setTextToShowWhenEmpty (jp (u8"空欄ならURLを知っている人だけ"), cMuted);
    mailEditor.setFont (juce::FontOptions ("Yu Gothic UI", 14.0f, juce::Font::plain));
    mailEditor.onTextChange = [this] { proc.setAllowedMails (mailEditor.getText().trim()); };
    addAndMakeVisible (mailEditor);

    styleLabel (remoteStatus, 12.5f, cMuted);
    styleLabel (remoteUrlLabel, 12.5f, cAmber);
    styleLabel (remoteInfo, 12.5f, cMuted);
    addAndMakeVisible (remoteQr);

    copyRemoteBtn.setButtonText (jp (u8"URLをコピー"));
    copyRemoteBtn.onClick = [this]
    {
        const auto u = proc.getRemoteShareUrl();
        if (u.isNotEmpty())
            juce::SystemClipboard::copyTextToClipboard (u);
    };
    addAndMakeVisible (copyRemoteBtn);

    downloadBtn.setButtonText (jp (u8"cloudflaredを取得"));
    downloadBtn.onClick = [this] { proc.getTunnel().downloadCloudflared(); };
    addChildComponent (downloadBtn);

    refreshAddresses();
    setSize (760, 440);
    timerCallback();
    startTimerHz (4);
}

PhoneMonitorEditor::~PhoneMonitorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PhoneMonitorEditor::refreshAddresses()
{
    juce::Array<juce::IPAddress> all;
    juce::IPAddress::findAllAddresses (all, false);
    juce::StringArray found;
    for (auto& a : all)
    {
        const auto s = a.toString();
        if (s.startsWith ("127.") || s.startsWith ("169.254.") || s == "0.0.0.0")
            continue;
        found.addIfNotAlreadyThere (s);
    }
    std::stable_sort (found.begin(), found.end(),
                      [] (const juce::String& a, const juce::String& b) { return addressRank (a) < addressRank (b); });
    addresses = found;

    const auto pref = proc.getPreferredAddress();
    addressIndex = juce::jmax (0, addresses.indexOf (pref));
}

juce::String PhoneMonitorEditor::lanUrl() const
{
    if (addresses.isEmpty() || proc.getServer().getPort() == 0)
        return {};
    return "http://" + addresses[addressIndex] + ":" + juce::String (proc.getServer().getPort());
}

void PhoneMonitorEditor::timerCallback()
{
    auto& server = proc.getServer();
    auto& tunnel = proc.getTunnel();

    // LAN
    const auto url = lanUrl();
    if (server.getPort() == 0)
        lanUrlLabel.setText (jp (u8"サーバーを起動できませんでした"), juce::dontSendNotification);
    else if (url.isEmpty())
        lanUrlLabel.setText (jp (u8"ネットワークが見つかりません"), juce::dontSendNotification);
    else
        lanUrlLabel.setText (url, juce::dontSendNotification);
    lanQr.setText (url);
    switchAddrBtn.setEnabled (addresses.size() > 1);
    lanInfo.setText (jp (u8"聴いている人：") + juce::String (server.getLanClientCount()) + jp (u8"人\nスマホのカメラでQRを読み取ってください。"),
                     juce::dontSendNotification);

    // 外部公開
    const bool active = proc.isRemoteActive();
    const auto st = tunnel.getState();
    remoteBtn.setButtonText (active ? jp (u8"外部公開を停止") : jp (u8"外部公開を開始"));
    remoteBtn.setToggleState (active, juce::dontSendNotification);
    mailEditor.setEnabled (! active);

    const auto shareUrl = proc.getRemoteShareUrl();
    remoteQr.setText (shareUrl);
    copyRemoteBtn.setEnabled (shareUrl.isNotEmpty());
    remoteUrlLabel.setText (shareUrl, juce::dontSendNotification);
    const bool showDownload = st == TunnelManager::State::notFound || st == TunnelManager::State::downloading
                              || (st == TunnelManager::State::failed && ! TunnelManager::findExecutable().existsAsFile());
    if (showDownload != downloadBtn.isVisible())
    {
        downloadBtn.setVisible (showDownload);
        resized();
    }
    downloadBtn.setEnabled (st != TunnelManager::State::downloading);

    juce::String status;
    juce::Colour col = cMuted;
    switch (st)
    {
        case TunnelManager::State::running:
            status = jp (u8"公開中：URLを相手に送ってください。停止するとURLは使えなくなります。");
            col = cGreen;
            break;
        case TunnelManager::State::starting:    status = tunnel.getMessage(); break;
        case TunnelManager::State::downloading: status = tunnel.getMessage(); break;
        case TunnelManager::State::notFound:
        case TunnelManager::State::failed:      status = tunnel.getMessage(); col = cRed; break;
        case TunnelManager::State::idle:
        default:
            status = tunnel.getMessage().isNotEmpty() ? tunnel.getMessage()
                                                      : jp (u8"Cloudflareの一時URLで、別の場所にいる人のブラウザに音を届けます。");
            break;
    }
    remoteStatus.setText (status, juce::dontSendNotification);
    remoteStatus.setColour (juce::Label::textColourId, col);
    remoteInfo.setText (active ? jp (u8"聴いている人：") + juce::String (server.getRemoteClientCount()) + jp (u8"人") : juce::String(),
                        juce::dontSendNotification);
}

void PhoneMonitorEditor::paint (juce::Graphics& g)
{
    g.fillAll (cBg);
    g.setColour (cMuted);
    g.setFont (juce::FontOptions ("Yu Gothic UI", 15.0f, juce::Font::bold));
    g.drawText ("PhoneMonitor", 20, 12, 200, 24, juce::Justification::centredLeft);

    for (auto r : { lanPanel, remotePanel })
    {
        g.setColour (cPanel);
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setColour (cLine);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 14.0f, 1.0f);
    }
}

void PhoneMonitorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto top = area.removeFromTop (28);
    sendToggle.setBounds (top.removeFromRight (150));
    area.removeFromTop (10);

    lanPanel = area.removeFromLeft (290);
    area.removeFromLeft (14);
    remotePanel = area;

    {
        auto r = lanPanel.reduced (16);
        lanTitle.setBounds (r.removeFromTop (24));
        lanUrlLabel.setBounds (r.removeFromTop (22));
        r.removeFromTop (6);
        lanQr.setBounds (r.removeFromTop (170).withSizeKeepingCentre (170, 170));
        r.removeFromTop (10);
        auto btns = r.removeFromTop (30);
        copyLanBtn.setBounds (btns.removeFromLeft (btns.getWidth() / 2 - 4));
        btns.removeFromLeft (8);
        switchAddrBtn.setBounds (btns);
        r.removeFromTop (8);
        lanInfo.setBounds (r);
    }
    {
        auto r = remotePanel.reduced (16);
        remoteTitle.setBounds (r.removeFromTop (24));
        r.removeFromTop (4);

        auto left = r.removeFromLeft (180);
        r.removeFromLeft (14);

        remoteBtn.setBounds (left.removeFromTop (32));
        left.removeFromTop (10);
        mailLabel.setBounds (left.removeFromTop (34));
        mailEditor.setBounds (left.removeFromTop (28));
        left.removeFromTop (10);
        downloadBtn.setBounds (left.removeFromTop (30));
        remoteStatus.setBounds (downloadBtn.isVisible() ? left.withTrimmedTop (8) : left.withTop (downloadBtn.getY()));

        remoteQr.setBounds (r.removeFromTop (170).withSizeKeepingCentre (170, 170));
        r.removeFromTop (10);
        copyRemoteBtn.setBounds (r.removeFromTop (30));
        r.removeFromTop (6);
        remoteUrlLabel.setBounds (r.removeFromTop (48));
        remoteInfo.setBounds (r);
    }
}
