#pragma once

// SelectionModel tracks keyboard selection focus across Antiphon UI sections:
// Local Channels and Remote Users.
//
// Pure and header-only so it can be tested directly without GUI dependencies.

namespace Selection {

enum class Section { LocalChannels, RemoteUsers };

class Model {
public:
  Section getSection() const { return currentSection; }
  void setSection(Section s) { currentSection = s; }

  int getLocalIndex() const { return localIndex; }
  void setLocalIndex(int index) { localIndex = index; }

  int getRemoteIndex() const { return remoteIndex; }
  void setRemoteIndex(int index) { remoteIndex = index; }

  void selectNextLocal(int count) {
    currentSection = Section::LocalChannels;
    if (count <= 0) {
      localIndex = 0;
      return;
    }
    localIndex = (localIndex + 1) % count;
  }

  void selectPrevLocal(int count) {
    currentSection = Section::LocalChannels;
    if (count <= 0) {
      localIndex = 0;
      return;
    }
    localIndex = (localIndex - 1 + count) % count;
  }

  void selectNextRemote(int count) {
    currentSection = Section::RemoteUsers;
    if (count <= 0) {
      remoteIndex = 0;
      return;
    }
    remoteIndex = (remoteIndex + 1) % count;
  }

  void selectPrevRemote(int count) {
    currentSection = Section::RemoteUsers;
    if (count <= 0) {
      remoteIndex = 0;
      return;
    }
    remoteIndex = (remoteIndex - 1 + count) % count;
  }

  void clamp(int localCount, int remoteCount) {
    if (localCount <= 0) {
      localIndex = 0;
    } else if (localIndex >= localCount) {
      localIndex = localCount - 1;
    }

    if (remoteCount <= 0) {
      remoteIndex = 0;
    } else if (remoteIndex >= remoteCount) {
      remoteIndex = remoteCount - 1;
    }
  }

private:
  Section currentSection = Section::LocalChannels;
  int localIndex = 0;
  int remoteIndex = 0;
};

} // namespace Selection
