// The file dialog of the new editor interface (open, save, add images and sounds). It draws the data of the
// classic CFileBrowser: the list, the sorting, the preview and the actions (open a folder, open or save a
// file, delete, new folder) stay the classic code.
#include "xeditor.h"

#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/xclient/hud/xc_hud.h>
#include <game/client/components/xclient/ui/feedback.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/editor/editor.h>

#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>

#include <algorithm>
#include <cfloat>

using namespace XcUi;

void CXcEditorUi::RenderFileDialog()
{
	CEditor &E = *m_pEditor;
	CFileBrowser &B = E.m_FileBrowser;
	const STheme &t = Theme();
	const bool Open = E.m_Dialog == DIALOG_FILE;
	if(!Open)
	{
		m_FileDialogShown = false;
		return;
	}
	if(!m_FileDialogShown)
	{
		// a new dialog: the fields start from the classic state
		m_FileDialogShown = true;
		str_copy(m_aFileFilter, B.m_FilterInput.GetString());
		str_copy(m_aFileName, B.m_FilenameInput.GetString());
		m_FileFocus = true;
	}

	// a classic popup over the dialog (new folder, delete, overwrite) owns the keyboard
	// our own small card over the dialog (new folder, delete) blocks the dialog the same way
	const bool ClassicPopup = E.Ui()->IsPopupOpen() || m_FileSub != FILESUB_NONE;
	if(E.Ui()->IsPopupOpen() && ImGui::GetActiveID() != 0)
	{
		ImGui::ClearActiveID();
		m_FileFocus = true;
	}

	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const float Width = std::min(S(960.0f), Display.x - S(60.0f));
	const float Height = std::min(S(680.0f), Display.y - S(80.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, t.m_Border);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(16.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.0f), S(18.0f)));
	// the veil: a window behind the dialog that eats the clicks on the editor
	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(Display);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
	ImGui::Begin("##xe_file_veil", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::SetNextWindowPos(ImVec2((Display.x - Width) * 0.5f, (Display.y - Height) * 0.5f));
	ImGui::SetNextWindowSize(ImVec2(Width, Height));
	if(m_FileSub == FILESUB_NONE) // focusing a window every frame would stop typing in the card over it
		ImGui::SetNextWindowFocus();
	if(!ImGui::Begin("##xe_file", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::End();
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(2);
		return;
	}
	ImGui::BeginDisabled(m_FileSub != FILESUB_NONE);
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	ImFont *pSmall = Fonts().m_pSmall;
	ImFont *pLabel = Fonts().m_pLabel;
	const float Inner = Width - S(40.0f);

	// ---- header: title, where we are, tools
	Header(Tr(B.m_aTitle));
	{
		char aPath[IO_MAX_PATH_LENGTH];
		if(B.m_ShowingRoot)
			str_copy(aPath, Tr("Places"));
		else if(B.m_SelectedFileIndex >= 0 && B.m_vpFilteredFileList[B.m_SelectedFileIndex]->m_StorageType >= IStorage::TYPE_SAVE)
			E.Storage()->GetCompletePath(B.m_vpFilteredFileList[B.m_SelectedFileIndex]->m_StorageType, B.m_pCurrentPath, aPath, sizeof(aPath));
		else
			str_copy(aPath, B.m_pCurrentPath);
		char aFit[IO_MAX_PATH_LENGTH];
		XcFitText(pSmall, pSmall->FontSize, aPath, Inner - S(200.0f), aFit, sizeof(aFit));
		ImGui::PushFont(pSmall);
		ImGui::TextColored(t.m_TextMuted, "%s %s", FontIcon::FOLDER_OPEN, aFit);
		ImGui::PopFont();
	}
	// refresh, show folder and new folder on the right of the title
	{
		const ImVec2 Saved = ImGui::GetCursorScreenPos();
		const float ButtonSize = S(32.0f);
		const ImVec2 WinPos = ImGui::GetWindowPos();
		float x = WinPos.x + Width - S(20.0f) - ButtonSize;
		ImGui::SetCursorScreenPos(ImVec2(x, WinPos.y + S(20.0f)));
		if(IconButton("##refresh", FontIcon::ARROWS_ROTATE, Tr("Refresh the list (F5)"), false, true, ButtonSize))
			B.FilelistPopulate(B.m_StorageType, true);
		const bool CanShowFolder = B.m_SelectedFileIndex >= 0 && B.m_vpFilteredFileList[B.m_SelectedFileIndex]->m_StorageType != IStorage::TYPE_ALL;
		x -= ButtonSize + S(4.0f);
		ImGui::SetCursorScreenPos(ImVec2(x, WinPos.y + S(20.0f)));
		if(IconButton("##showdir", FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, Tr("Open this folder in the system"), false, CanShowFolder, ButtonSize))
		{
			char aOpenPath[IO_MAX_PATH_LENGTH];
			E.Storage()->GetCompletePath(B.m_vpFilteredFileList[B.m_SelectedFileIndex]->m_StorageType, B.m_pCurrentPath, aOpenPath, sizeof(aOpenPath));
			if(!E.Client()->ViewFile(aOpenPath))
				E.ShowFileDialogError("Failed to open the directory '%s'.", aOpenPath);
		}
		if(!B.m_ShowingRoot && B.m_StorageType == IStorage::TYPE_SAVE)
		{
			x -= ButtonSize + S(4.0f);
			ImGui::SetCursorScreenPos(ImVec2(x, WinPos.y + S(20.0f)));
			if(IconButton("##newfolder", FontIcon::FOLDER, Tr("New folder"), false, true, ButtonSize))
			{
				m_FileSub = FILESUB_NEW_FOLDER;
				m_aNewFolder[0] = '\0';
				m_FileSubFocus = true;
			}
		}
		ImGui::SetCursorScreenPos(Saved);
	}
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));

	// ---- search when opening, the name when saving
	if(m_FileFocus && !ClassicPopup)
	{
		ImGui::SetKeyboardFocusHere();
		m_FileFocus = false;
	}
	if(B.m_SaveAction)
	{
		if(InputText("##filename", m_aFileName, sizeof(m_aFileName), Tr("File name"), Inner))
		{
			B.m_FilenameInput.Set(m_aFileName);
			B.UpdateSelectedIndex(m_aFileName);
		}
	}
	else if(InputText("##filter", m_aFileFilter, sizeof(m_aFileFilter), Tr("Search"), Inner))
	{
		// the same as the classic search field
		B.m_FilterInput.Set(m_aFileFilter);
		B.RefreshFilteredFileList();
		if(B.m_vpFilteredFileList.empty())
			B.m_SelectedFileIndex = -1;
		else if(B.m_SelectedFileIndex == -1 || (m_aFileFilter[0] && !str_find_nocase(B.m_vpFilteredFileList[B.m_SelectedFileIndex]->m_aDisplayName, m_aFileFilter)))
		{
			B.m_SelectedFileIndex = 0;
			for(size_t i = 0; i < B.m_vpFilteredFileList.size(); i++)
			{
				if(str_find_nocase(B.m_vpFilteredFileList[i]->m_aDisplayName, m_aFileFilter))
				{
					B.m_SelectedFileIndex = i;
					break;
				}
			}
		}
		str_copy(B.m_aSelectedFileDisplayName, B.m_SelectedFileIndex >= 0 ? B.m_vpFilteredFileList[B.m_SelectedFileIndex]->m_aDisplayName : "");
		B.UpdateFilenameInput();
		B.m_PreviewState = CFileBrowser::EPreviewState::UNLOADED;
	}
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

	// ---- the list and the preview
	const bool Preview = B.m_SelectedFileIndex >= 0 && B.CanPreviewFile();
	const float ListWidth = Preview ? Inner * 0.62f : Inner;
	const float ListHeight = ImGui::GetContentRegionAvail().y - S(52.0f);
	// sort headers
	{
		const char *const apArrows[] = {"", FontIcon::CHEVRON_UP, FontIcon::CHEVRON_DOWN};
		const ImVec2 HeaderPos = ImGui::GetCursorScreenPos();
		char aName[64], aTime[64];
		str_format(aName, sizeof(aName), "%s  %s", Tr("Name of the file"), apArrows[(int)B.m_SortByFilename]);
		str_format(aTime, sizeof(aTime), "%s  %s", Tr("Changed"), apArrows[(int)B.m_SortByTimeModified]);
		ImGui::SetCursorScreenPos(ImVec2(HeaderPos.x + S(38.0f), HeaderPos.y));
		ImGui::PushStyleColor(ImGuiCol_Text, t.m_TextMuted);
		ImGui::PushFont(pSmall);
		if(!B.m_ShowingRoot && ImGui::Selectable(aName, false, 0, ImVec2(ListWidth * 0.5f, 0.0f)))
		{
			B.m_SortByFilename = B.m_SortByFilename == CFileBrowser::ESortDirection::DESCENDING ? CFileBrowser::ESortDirection::ASCENDING : CFileBrowser::ESortDirection::DESCENDING;
			B.m_SortByTimeModified = CFileBrowser::ESortDirection::NEUTRAL;
			B.RefreshFilteredFileList();
		}
		ImGui::SetCursorScreenPos(ImVec2(HeaderPos.x + ListWidth - S(150.0f), HeaderPos.y));
		if(!B.m_ShowingRoot && ImGui::Selectable(aTime, false, 0, ImVec2(S(140.0f), 0.0f)))
		{
			B.m_SortByTimeModified = B.m_SortByTimeModified == CFileBrowser::ESortDirection::ASCENDING ? CFileBrowser::ESortDirection::DESCENDING : (B.m_SortByTimeModified == CFileBrowser::ESortDirection::DESCENDING ? CFileBrowser::ESortDirection::NEUTRAL : CFileBrowser::ESortDirection::ASCENDING);
			B.RefreshFilteredFileList();
		}
		ImGui::PopFont();
		ImGui::PopStyleColor();
		ImGui::SetCursorScreenPos(ImVec2(HeaderPos.x, HeaderPos.y + pSmall->FontSize + S(8.0f)));
	}
	const ImVec2 ListPos = ImGui::GetCursorScreenPos();
	pDraw->AddRectFilled(ListPos, ImVec2(ListPos.x + ListWidth, ListPos.y + ListHeight - pSmall->FontSize - S(8.0f)), Col(t.m_Track, 0.45f), S(10.0f));
	ImGui::BeginChild("##files", ImVec2(ListWidth, ListHeight - pSmall->FontSize - S(8.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	bool Activate = false;
	{
		ImDrawList *pListDraw = ImGui::GetWindowDrawList();
		const float RowHeight = S(32.0f);
		const float RowWidth = ImGui::GetContentRegionAvail().x;
		const int OldSelection = B.m_SelectedFileIndex;
		ImGuiListClipper Clipper;
		Clipper.Begin((int)B.m_vpFilteredFileList.size(), RowHeight);
		while(Clipper.Step())
		{
			for(int i = Clipper.DisplayStart; i < Clipper.DisplayEnd; i++)
			{
				const auto *pItem = B.m_vpFilteredFileList[i];
				ImGui::PushID(i);
				const ImVec2 Min = ImGui::GetCursorScreenPos();
				const ImVec2 Max(Min.x + RowWidth, Min.y + RowHeight);
				if(ImGui::InvisibleButton("##file", ImVec2(RowWidth, RowHeight)))
				{
					B.m_SelectedFileIndex = i;
				}
				const bool Hovered = ImGui::IsItemHovered();
				if(Hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					B.m_SelectedFileIndex = i;
					Activate = true;
				}
				const bool Selected = B.m_SelectedFileIndex == i;
				if(Selected)
					pListDraw->AddRectFilled(Min, Max, Col(t.m_Accent, 0.22f), S(7.0f));
				else if(Hovered)
					pListDraw->AddRectFilled(Min, Max, Col(t.m_Card, 1.0f), S(7.0f));
				const char *pIcon = B.DetermineFileFontIcon(pItem);
				const ImVec4 IconColor = pItem->m_IsDir ? ImVec4(1.0f, 0.78f, 0.35f, 1.0f) : t.m_TextSecondary;
				pListDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Min.x + S(12.0f), Min.y + (RowHeight - pLabel->FontSize) * 0.5f), Col(IconColor), pIcon);
				char aName[IO_MAX_PATH_LENGTH];
				XcFitText(pSmall, pSmall->FontSize, pItem->m_aDisplayName, RowWidth - S(200.0f), aName, sizeof(aName));
				pListDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + S(38.0f), Min.y + (RowHeight - pSmall->FontSize) * 0.5f), Col(Selected ? t.m_Text : t.m_TextSecondary), aName);
				if(!pItem->m_IsLink && str_comp(pItem->m_aFilename, "..") != 0)
				{
					char aTime[64];
					str_timestamp_ex(pItem->m_TimeModified, aTime, sizeof(aTime), "%d.%m.%Y %H:%M");
					const float TimeWidth = pSmall->CalcTextSizeA(pSmall->FontSize * 0.9f, FLT_MAX, 0.0f, aTime).x;
					pListDraw->AddText(pSmall, pSmall->FontSize * 0.9f, ImVec2(Max.x - TimeWidth - S(12.0f), Min.y + (RowHeight - pSmall->FontSize * 0.9f) * 0.5f), Col(t.m_TextMuted), aTime);
				}
				ImGui::PopID();
			}
		}
		// arrows move the selection like in the classic list
		if(!B.m_vpFilteredFileList.empty() && !ClassicPopup)
		{
			if(ImGui::IsKeyPressed(ImGuiKey_DownArrow))
				B.m_SelectedFileIndex = std::min(B.m_SelectedFileIndex + 1, (int)B.m_vpFilteredFileList.size() - 1);
			if(ImGui::IsKeyPressed(ImGuiKey_UpArrow))
				B.m_SelectedFileIndex = std::max(B.m_SelectedFileIndex - 1, 0);
		}
		if(B.m_SelectedFileIndex != OldSelection)
		{
			str_copy(B.m_aSelectedFileDisplayName, B.m_SelectedFileIndex >= 0 ? B.m_vpFilteredFileList[B.m_SelectedFileIndex]->m_aDisplayName : "");
			B.UpdateFilenameInput();
			str_copy(m_aFileName, B.m_FilenameInput.GetString());
			B.m_PreviewState = CFileBrowser::EPreviewState::UNLOADED;
			if(B.m_SelectedFileIndex >= 0)
				ImGui::SetScrollY(std::max(0.0f, B.m_SelectedFileIndex * RowHeight - ImGui::GetWindowHeight() * 0.5f));
		}
		if(B.m_vpFilteredFileList.empty())
			Description(Tr("Nothing here"));
	}
	ImGui::EndChild();

	if(Preview)
	{
		// the picture of the selected image file
		ImGui::SameLine(0.0f, S(16.0f));
		const ImVec2 PreviewPos = ImGui::GetCursorScreenPos();
		const float PreviewWidth = Inner - ListWidth - S(16.0f);
		const float PreviewHeight = ListHeight - pSmall->FontSize - S(8.0f);
		pDraw->AddRectFilled(PreviewPos, ImVec2(PreviewPos.x + PreviewWidth, PreviewPos.y + PreviewHeight), Col(t.m_Track, 0.45f), S(10.0f));
		B.UpdateFilePreview();
		if(B.m_PreviewState == CFileBrowser::EPreviewState::LOADED && B.m_FileType == CFileBrowser::EFileType::IMAGE && B.m_PreviewImage.IsValid())
		{
			const float Box = std::min(PreviewWidth, PreviewHeight - S(40.0f)) - S(24.0f);
			const float Aspect = B.m_PreviewImageHeight > 0 ? (float)B.m_PreviewImageWidth / B.m_PreviewImageHeight : 1.0f;
			const float w = Aspect >= 1.0f ? Box : Box * Aspect;
			const float h = Aspect >= 1.0f ? Box / Aspect : Box;
			const ImVec2 PicMin(PreviewPos.x + (PreviewWidth - w) * 0.5f, PreviewPos.y + S(12.0f) + (Box - h) * 0.5f);
			pDraw->AddImage((ImTextureID)(uintptr_t)m_Gui.TextureId(B.m_PreviewImage), PicMin, ImVec2(PicMin.x + w, PicMin.y + h));
			char aSize[64];
			str_format(aSize, sizeof(aSize), "%d × %d", B.m_PreviewImageWidth, B.m_PreviewImageHeight);
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(PreviewPos.x + S(12.0f), PreviewPos.y + PreviewHeight - pSmall->FontSize - S(12.0f)), Col(t.m_TextMuted), aSize);
		}
		else if(B.m_PreviewState == CFileBrowser::EPreviewState::ERROR)
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(PreviewPos.x + S(12.0f), PreviewPos.y + S(12.0f)), Col(ImVec4(1.0f, 0.5f, 0.5f, 1.0f)), Tr("No preview"));
		else if(B.m_FileType == CFileBrowser::EFileType::SOUND)
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(PreviewPos.x + S(12.0f), PreviewPos.y + S(12.0f)), Col(t.m_TextMuted), Tr("A sound file"));
		ImGui::Dummy(ImVec2(PreviewWidth, PreviewHeight));
	}

	// ---- buttons
	ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
	const bool IsDir = B.m_SelectedFileIndex >= 0 && B.m_vpFilteredFileList[B.m_SelectedFileIndex]->m_IsDir;
	const bool CanDelete = B.m_SelectedFileIndex >= 0 && B.m_vpFilteredFileList[B.m_SelectedFileIndex]->m_StorageType == IStorage::TYPE_SAVE &&
			       !B.m_vpFilteredFileList[B.m_SelectedFileIndex]->m_IsLink && str_comp(B.m_vpFilteredFileList[B.m_SelectedFileIndex]->m_aFilename, "..") != 0;
	const float ButtonWidth = S(150.0f);
	if(CanDelete)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
		if(Button(Tr("Delete"), false, S(110.0f)))
		{
			m_FileSub = FILESUB_DELETE;
			m_FileDeleteIsDir = IsDir;
			str_format(m_aFileDelete, sizeof(m_aFileDelete), "%s/%s", B.m_pCurrentPath, B.m_vpFilteredFileList[B.m_SelectedFileIndex]->m_aFilename);
		}
		ImGui::PopStyleColor();
		ImGui::SameLine();
	}
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - S(20.0f) - ButtonWidth * 2.0f - S(8.0f));
	const bool Escape = !ClassicPopup && ImGui::IsKeyPressed(ImGuiKey_Escape);
	bool Close = false;
	if(Button(Tr("Cancel"), false, ButtonWidth) || Escape)
		Close = true;
	ImGui::SameLine(0.0f, S(8.0f));
	if(Button(IsDir ? Tr("Open") : Tr(B.m_aButtonText), true, ButtonWidth) || (!ClassicPopup && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))))
		Activate = true;

	ImGui::EndDisabled();
	ImGui::End();
	if(m_FileSub != FILESUB_NONE)
		RenderFileSubDialog();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(2);

	if(Close)
	{
		E.OnDialogClose();
		return;
	}
	if(Activate && !ClassicPopup)
	{
		B.XcOpenSelected();
		// a new folder: the fields follow it
		str_copy(m_aFileFilter, B.m_FilterInput.GetString());
		str_copy(m_aFileName, B.m_FilenameInput.GetString());
	}
}

void CXcEditorUi::RenderFileSubDialog()
{
	CEditor &E = *m_pEditor;
	CFileBrowser &B = E.m_FileBrowser;
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const float Width = S(420.0f);
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));
	ImGui::SetNextWindowFocus();
	ImGui::Begin("##xe_file_sub", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
	const float Inner = Width - S(40.0f);
	const float ButtonWidth = (Inner - S(8.0f)) * 0.5f;
	bool Close = ImGui::IsKeyPressed(ImGuiKey_Escape);
	const bool Enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
	if(m_FileSub == FILESUB_NEW_FOLDER)
	{
		Header(Tr("New folder"));
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		if(m_FileSubFocus && !ImGui::IsWindowAppearing()) // an auto-sized window skips its items in the first frame
		{
			ImGui::SetKeyboardFocusHere();
			m_FileSubFocus = false;
		}
		InputText("##newfolder", m_aNewFolder, sizeof(m_aNewFolder), Tr("Folder name"), Inner);
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		if(Button(Tr("Cancel"), false, ButtonWidth))
			Close = true;
		ImGui::SameLine(0.0f, S(8.0f));
		if((Button(Tr("Create"), true, ButtonWidth) || Enter) && m_aNewFolder[0])
		{
			// the same as the classic popup
			char aFolderPath[IO_MAX_PATH_LENGTH];
			str_format(aFolderPath, sizeof(aFolderPath), "%s/%s", B.m_pCurrentPath, m_aNewFolder);
			if(!str_valid_filename(m_aNewFolder))
				Toast(Tr("This name cannot be used for files and folders"), TOAST_ERROR);
			else if(!E.Storage()->CreateFolder(aFolderPath, B.m_StorageType))
				Toast(Tr("Failed to create the folder"), TOAST_ERROR);
			else
			{
				char aFolderDisplayName[IO_MAX_PATH_LENGTH];
				str_format(aFolderDisplayName, sizeof(aFolderDisplayName), "%s/", m_aNewFolder);
				B.FilelistPopulate(B.m_StorageType, false);
				B.UpdateSelectedIndex(aFolderDisplayName);
				Close = true;
			}
		}
	}
	else
	{
		Header(Tr(m_FileDeleteIsDir ? "Delete the folder?" : "Delete the file?"));
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Inner);
		Description(m_aFileDelete);
		if(m_FileDeleteIsDir)
			Description(Tr("Only an empty folder can be deleted"));
		ImGui::PopTextWrapPos();
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		if(Button(Tr("Cancel"), false, ButtonWidth))
			Close = true;
		ImGui::SameLine(0.0f, S(8.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
		const bool Delete = Button(Tr("Delete"), false, ButtonWidth) || Enter;
		ImGui::PopStyleColor();
		if(Delete)
		{
			const bool Done = m_FileDeleteIsDir ? E.Storage()->RemoveFolder(m_aFileDelete, IStorage::TYPE_SAVE) : E.Storage()->RemoveFile(m_aFileDelete, IStorage::TYPE_SAVE);
			if(Done)
			{
				B.FilelistPopulate(IStorage::TYPE_SAVE, true);
				Toast(Tr("Deleted"), TOAST_SUCCESS);
			}
			else
				Toast(Tr(m_FileDeleteIsDir ? "Failed to delete the folder, make sure it is empty" : "Failed to delete the file"), TOAST_ERROR);
			B.UpdateFilenameInput();
			str_copy(m_aFileName, B.m_FilenameInput.GetString());
			Close = true;
		}
	}
	ImGui::End();
	if(Close)
	{
		m_FileSub = FILESUB_NONE;
		m_FileFocus = true;
	}
}
