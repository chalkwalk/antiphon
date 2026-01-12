#include "ServerBrowserDialog.h"

static const ServerBrowserDialog::ServerEntry kStaticServers[] = {
    {"ninbot.com",  2049},
    {"ninjam.com",  2049},
    {"autojam.com", 2049},
};

ServerBrowserDialog::ServerBrowserDialog()
    : table("Servers", this) {
  // The dialog is an overlay child of the editor, not a modal window. It must
  // both accept keyboard focus itself and swallow clicks, or focus and mouse
  // events leak through to the mixer behind it.
  setWantsKeyboardFocus(true);
  setInterceptsMouseClicks(true, true);


  closeButton.onClick = [this]() { dismiss(); };
  addAndMakeVisible(closeButton);

  table.getHeader().addColumn("Server",  1, 230);
  table.getHeader().addColumn("BPM",     2,  55);
  table.getHeader().addColumn("BPI",     3,  55);
  table.getHeader().addColumn("Players", 4, 310);
  table.setHeaderHeight(28);
  table.setRowHeight(24);
  table.setColour(juce::TableListBox::backgroundColourId,
                  juce::Colour(0xff111122));
  table.setOutlineThickness(1);
  addAndMakeVisible(table);

  hostInput.setText("ninbot.com");
  addAndMakeVisible(hostInput);

  portInput.setText("2049");
  portInput.setInputRestrictions(5, "0123456789");
  addAndMakeVisible(portInput);

  usernameInput.setTextToShowWhenEmpty("Nickname", juce::Colours::grey);
  addAndMakeVisible(usernameInput);

  passwordInput.setPasswordCharacter('*');
  passwordInput.setTextToShowWhenEmpty("Password", juce::Colours::grey);
  addChildComponent(passwordInput);

  anonymousToggle.setToggleState(true, juce::dontSendNotification);
  anonymousToggle.onClick = [this]() {
    passwordInput.setVisible(!anonymousToggle.getToggleState());
  };
  addAndMakeVisible(anonymousToggle);

  connectButton.onClick = [this]() {
    juce::String host = hostInput.getText().trim();
    int port = portInput.getText().getIntValue();
    if (port <= 0) port = 2049;
    juce::String nick = usernameInput.getText().trim();
    juce::String user, pass;
    if (anonymousToggle.getToggleState()) {
      user = (nick.isEmpty() || nick.startsWithIgnoreCase("anonymous"))
                 ? (nick.isEmpty() ? "anonymous" : nick)
                 : "anonymous:" + nick;
    } else {
      user = nick;
      pass = passwordInput.getText();
    }
    if (onConnect)
      onConnect(host, port, user, pass);
    dismiss();
  };
  addAndMakeVisible(connectButton);

  cancelButton.onClick = [this]() { dismiss(); };
  addAndMakeVisible(cancelButton);

  statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
  statusLabel.setText("Fetching server list...", juce::dontSendNotification);
  addAndMakeVisible(statusLabel);

  populateStaticList();
  fetchThread = std::thread([this]() { doFetch(); });
}

ServerBrowserDialog::~ServerBrowserDialog() {
  stopFetch.store(true);
  fetchSocket.close();
  if (fetchThread.joinable())
    fetchThread.detach();
}

void ServerBrowserDialog::dismiss() {
  if (onClose)
    onClose();
}

void ServerBrowserDialog::populateStaticList() {
  servers.clear();
  for (auto &s : kStaticServers)
    servers.add(s);
}

juce::Array<ServerBrowserDialog::ServerEntry>
ServerBrowserDialog::parseServersJSON(const juce::String &json) {
  juce::Array<ServerEntry> result;
  juce::var parsed = juce::JSON::parse(json);

  // API returns {"servers": [...]}; all numeric fields are quoted strings.
  juce::var arr = parsed["servers"];
  if (!arr.isArray())
    return result;

  for (int i = 0; i < arr.size(); ++i) {
    const auto &obj = arr[i];
    ServerEntry e;
    e.host = obj["host"].toString();
    e.port = obj["port"].toString().getIntValue();
    e.bpm  = obj["bpm"].toString().getIntValue();
    e.bpi  = obj["bpi"].toString().getIntValue();

    const auto &users = obj["users"];
    if (users.isArray() && users.size() > 0) {
      juce::StringArray names;
      for (int j = 0; j < users.size(); ++j)
        names.add(users[j]["name"].toString());
      e.players = names.joinIntoString(", ");
    } else {
      int count = obj["user_count"].toString().getIntValue();
      if (count > 0)
        e.players = juce::String(count) + (count == 1 ? " player" : " players");
    }

    if (e.host.isNotEmpty())
      result.add(e);
  }
  return result;
}

void ServerBrowserDialog::doFetch() {
  if (!fetchSocket.connect("ninbot.com", 80, 3000) || stopFetch.load()) {
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<ServerBrowserDialog>(this)]() {
          if (safe)
            safe->statusLabel.setText(
                "Could not reach ninbot.com -- showing known servers.",
                juce::dontSendNotification);
        });
    return;
  }

  const juce::String req =
      "GET /app/servers.php HTTP/1.0\r\nHost: ninbot.com\r\nConnection: "
      "close\r\n\r\n";
  fetchSocket.write(req.toRawUTF8(), (int)req.getNumBytesAsUTF8());

  juce::MemoryBlock response;
  char buf[4096];
  while (!stopFetch.load()) {
    int ready = fetchSocket.waitUntilReady(true, 200);
    if (ready < 0) break;
    if (ready == 0) continue;
    int n = fetchSocket.read(buf, sizeof(buf), false);
    if (n <= 0) break;
    response.append(buf, n);
  }

  if (stopFetch.load() || response.getSize() == 0)
    return;

  juce::String text = juce::String::fromUTF8(
      static_cast<const char *>(response.getData()), (int)response.getSize());
  int bodyStart = text.indexOf("\r\n\r\n");
  if (bodyStart < 0)
    return;

  auto entries = parseServersJSON(text.substring(bodyStart + 4));

  juce::MessageManager::callAsync(
      [safe = juce::Component::SafePointer<ServerBrowserDialog>(this),
       entries = std::move(entries)]() mutable {
        if (!safe) return;
        if (entries.isEmpty()) {
          safe->statusLabel.setText(
              "No live data -- showing known servers.",
              juce::dontSendNotification);
        } else {
          safe->servers = entries;
          safe->table.updateContent();
          safe->statusLabel.setText("", juce::dontSendNotification);
        }
      });
}

void ServerBrowserDialog::paint(juce::Graphics &g) {
  // Overall background + border
  g.fillAll(juce::Colour(0xff0d0d1a));
  g.setColour(juce::Colour(0xff00b4d8));
  g.drawRect(getLocalBounds(), 1);

  // Title bar
  auto titleBar = getLocalBounds().removeFromTop(kTitleBarH);
  g.setColour(juce::Colour(0xff111122));
  g.fillRect(titleBar);
  g.setColour(juce::Colour(0xff00b4d8));
  g.drawLine(0.0f, (float)kTitleBarH, (float)getWidth(), (float)kTitleBarH, 1.0f);

  g.setFont(juce::FontOptions{}.withHeight(14.0f).withStyle("Bold"));
  g.setColour(juce::Colours::white);
  g.drawFittedText("Server Browser",
                   titleBar.reduced(8, 0).withTrimmedRight(kTitleBarH),
                   juce::Justification::centredLeft, 1);
}

void ServerBrowserDialog::resized() {
  auto area = getLocalBounds().reduced(1); // inside border

  // Title bar row -- closeButton sits in it
  auto titleBar = area.removeFromTop(kTitleBarH);
  closeButton.setBounds(titleBar.removeFromRight(kTitleBarH).reduced(4));

  area.reduce(9, 9); // inner padding

  // Status label at very bottom
  statusLabel.setBounds(area.removeFromBottom(20));
  area.removeFromBottom(4);

  // Button row
  auto btnRow = area.removeFromBottom(28);
  cancelButton.setBounds(btnRow.removeFromRight(90).reduced(0, 3));
  btnRow.removeFromRight(8);
  connectButton.setBounds(btnRow.removeFromRight(90).reduced(0, 3));

  area.removeFromBottom(8);

  // Credentials row
  auto credRow = area.removeFromBottom(26);
  hostInput.setBounds(credRow.removeFromLeft(180).reduced(0, 3));
  credRow.removeFromLeft(6);
  portInput.setBounds(credRow.removeFromLeft(55).reduced(0, 3));
  credRow.removeFromLeft(10);
  usernameInput.setBounds(credRow.removeFromLeft(130).reduced(0, 3));
  credRow.removeFromLeft(8);
  anonymousToggle.setBounds(credRow.removeFromLeft(100).reduced(0, 3));
  credRow.removeFromLeft(6);
  passwordInput.setBounds(credRow.removeFromLeft(130).reduced(0, 3));

  area.removeFromBottom(8);

  // Table fills remaining space
  table.setBounds(area);
}

// --- TableListBoxModel ---

int ServerBrowserDialog::getNumRows() { return servers.size(); }

void ServerBrowserDialog::paintRowBackground(juce::Graphics &g, int /*row*/,
                                             int /*w*/, int /*h*/,
                                             bool selected) {
  g.fillAll(selected ? juce::Colour(0xff00b4d8).withAlpha(0.30f)
                     : juce::Colour(0xff111122));
}

void ServerBrowserDialog::paintCell(juce::Graphics &g, int row, int col,
                                    int w, int h, bool /*selected*/) {
  if (row >= servers.size()) return;
  const auto &s = servers.getReference(row);
  juce::String text;
  switch (col) {
  case 1: text = s.host + ":" + juce::String(s.port); break;
  case 2: text = s.bpm > 0 ? juce::String(s.bpm) : "-"; break;
  case 3: text = s.bpi > 0 ? juce::String(s.bpi) : "-"; break;
  case 4: text = s.players; break;
  default: break;
  }
  int textW = w - 8;
  if (textW <= 0) return;
  g.setColour(juce::Colours::white);
  g.setFont(juce::FontOptions{}.withHeight(13.0f));
  g.drawFittedText(text, 4, 0, textW, h, juce::Justification::centredLeft, 1);
}

void ServerBrowserDialog::cellClicked(int row, int /*col*/,
                                      const juce::MouseEvent &) {
  if (row >= servers.size()) return;
  const auto &s = servers.getReference(row);
  hostInput.setText(s.host, juce::dontSendNotification);
  portInput.setText(juce::String(s.port), juce::dontSendNotification);
}
