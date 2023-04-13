/*
 * Created on Wed Apr 05 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useEffect, useImperativeHandle, useRef, useState } from 'react';
import { Grid, makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';
import { toast } from 'react-toastify';
import { Descendant, Transforms } from 'slate';
import { HistoryEditor } from 'slate-history';
import { ReactEditor } from 'slate-react';
import { debounce, find } from 'lodash';
import { YBLoadingCircleIcon } from '../../../../../../components/common/indicators';
import { YBButton } from '../../../../../components';
import { YBEditor } from '../../../../../components/YBEditor';
import {
  ALERT_VARIABLE_END_TAG,
  ALERT_VARIABLE_START_TAG,
  clearEditor,
  DefaultElement,
  isBlockActive,
  isEditorDirty,
  isMarkActive,
  IYBEditor,
  TextDecorators,
  toggleBlock,
  toggleMark
} from '../../../../../components/YBEditor/plugins';
import { HTMLDeSerializer, HTMLSerializer } from '../../../../../components/YBEditor/serializers';
import EmailPreviewModal from './EmailPreviewModal';
import { useCommonStyles } from '../../CommonStyles';
import {
  ALERT_TEMPLATES_QUERY_KEY,
  createAlertChannelTemplates,
  getAlertChannelTemplates
} from '../../CustomVariablesAPI';
import { AlertPopover, GetInsertVariableButton, useComposerStyles } from '../ComposerStyles';
import { IComposer, IComposerRef } from '../IComposer';
import { TextSerializer } from '../../../../../components/YBEditor/serializers/Text/TextSerializer';
import { TextDeserializer } from '../../../../../components/YBEditor/serializers/Text/TextDeSerializer';

//icons
import { Info } from '@material-ui/icons';

import { ReactComponent as Italic } from '../icons/italic.svg';
import { ReactComponent as Bold } from '../icons/bold.svg';
import { ReactComponent as Underline } from '../icons/underline.svg';
import { ReactComponent as Strikethrough } from '../icons/strikethrough.svg';
import { FormatAlignCenter, FormatAlignLeft, FormatAlignRight } from '@material-ui/icons';

const ToolbarMarkIcons: Partial<Record<TextDecorators, { icon: React.ReactChild }>> = {
  italic: {
    icon: <Italic />
  },
  bold: {
    icon: <Bold />
  },
  underline: {
    icon: <Underline className="big" />
  },
  strikethrough: {
    icon: <Strikethrough className="big" />
  }
};

const ToolbarBlockIcons: Record<
  string,
  { icon: React.ReactElement; fn: Function; align: string }
> = {
  alignLeft: {
    icon: <FormatAlignLeft className="medium" />,
    fn: (editor: IYBEditor) => toggleBlock(editor, 'left'),
    align: 'left'
  },
  alignCenter: {
    icon: <FormatAlignCenter className="medium" />,
    fn: (editor: IYBEditor) => toggleBlock(editor, 'center'),
    align: 'center'
  },
  alignRight: {
    icon: <FormatAlignRight className="medium" />,
    fn: (editor: IYBEditor) => toggleBlock(editor, 'right'),
    align: 'right'
  }
};

const useStyles = makeStyles((theme) => ({
  composers: {
    padding: `0 ${theme.spacing(3.5)}px !important`
  }
}));

const EmailComposer = React.forwardRef<IComposerRef, React.PropsWithChildren<IComposer>>(
  ({ onClose }, forwardRef) => {
    const { t } = useTranslation();
    const commonStyles = useCommonStyles();
    const composerStyles = useComposerStyles();
    const classes = useStyles();
    const [subject, setSubject] = useState<Descendant[]>([]);
    const [body, setBody] = useState<Descendant[]>([]);

    const [showBodyAlertPopover, setShowBodyAlertPopover] = useState(false);
    const [showSubjectAlertPopover, setShowSubjectAlertPopover] = useState(false);
    const bodyInsertVariableButRef = useRef(null);

    const subjectEditorRef = useRef<IYBEditor | null>(null);
    const bodyEditorRef = useRef<IYBEditor | null>(null);

    const subjectInsertVariableButRef = useRef(null);

    const [showPreviewModal, setShowPreviewModal] = useState(false);

    // counter to force re-render this component, if any operations is performed on the body editor
    const [counter, setCounter] = useState(1);
    const reRender = debounce(function () {
      setCounter(counter + 1);
    }, 200);

    const { data: channelTemplates, isLoading: isTemplateLoading } = useQuery(
      ALERT_TEMPLATES_QUERY_KEY.getAlertChannelTemplates,
      getAlertChannelTemplates
    );

    const createTemplate = useMutation(
      ({ textTemplate, titleTemplate }: { textTemplate: string; titleTemplate: string }) => {
        return createAlertChannelTemplates({
          type: 'Email',
          textTemplate,
          titleTemplate
        });
      },
      {
        onSuccess: () => {
          toast.success(t('alertCustomTemplates.composer.templateSavedSuccess'));
        }
      }
    );

    useEffect(() => {
      if (isTemplateLoading) return;

      const emailTemplate = find(channelTemplates?.data, { type: 'Email' });

      if (emailTemplate && bodyEditorRef.current && subjectEditorRef.current) {
        try {
          let bodyVal = new HTMLDeSerializer(
            bodyEditorRef.current,
            emailTemplate.textTemplate ?? emailTemplate.defaultTextTemplate ?? ''
          ).deserialize();
          // this is not a html template, just a plain text
          if (bodyVal[0].text) {
            bodyVal = [
              {
                ...DefaultElement,
                children: bodyVal as any
              }
            ];
          }
          // Don't aleter the history while loading the template
          HistoryEditor.withoutSaving(bodyEditorRef.current, () => {
            clearEditor(bodyEditorRef.current as IYBEditor);
            Transforms.insertNodes(bodyEditorRef.current as IYBEditor, bodyVal);
          });

          const subjectVal = new TextDeserializer(
            subjectEditorRef.current,
            emailTemplate.titleTemplate ?? emailTemplate.defaultTitleTemplate ?? ''
          ).deserialize();
          HistoryEditor.withoutSaving(subjectEditorRef.current, () => {
            clearEditor(subjectEditorRef.current as IYBEditor);
            Transforms.insertNodes(subjectEditorRef.current as IYBEditor, subjectVal);
          });
        } catch (e) {
          console.log(e);
        }
      }
    }, [isTemplateLoading, channelTemplates]);

    useImperativeHandle(
      forwardRef,
      () => ({
        editors: {
          bodyEditor: bodyEditorRef,
          subjectEditor: subjectEditorRef
        }
      }),
      []
    );

    if (isTemplateLoading) {
      return <YBLoadingCircleIcon />;
    }

    return (
      <>
        <Grid className={classes.composers}>
          <Grid item alignItems="center" container className={composerStyles.subjectArea}>
            {t('alertCustomTemplates.composer.subject')}
            <Grid
              container
              item
              alignItems="center"
              className={clsx(
                commonStyles.editorBorder,
                commonStyles.subjectEditor,
                composerStyles.editorArea
              )}
            >
              <Grid item style={{ width: '80%' }}>
                <YBEditor
                  setVal={setSubject}
                  loadPlugins={{
                    singleLine: true,
                    alertVariablesPlugin: true,
                    basic: false,
                    defaultPlugin: true
                  }}
                  ref={subjectEditorRef}
                />
              </Grid>
              <Grid item style={{ width: '20%' }}>
                <GetInsertVariableButton
                  onClick={() => setShowSubjectAlertPopover(true)}
                  ref={subjectInsertVariableButRef}
                />
                <AlertPopover
                  anchorEl={subjectInsertVariableButRef.current}
                  editor={subjectEditorRef.current as any}
                  onVariableSelect={(variable, type) => {
                    if (!subjectEditorRef.current) return;
                    subjectEditorRef.current.insertText(
                      `${ALERT_VARIABLE_START_TAG}${variable.name}${ALERT_VARIABLE_END_TAG}`
                    );
                  }}
                  handleClose={() => {
                    setShowSubjectAlertPopover(false);
                  }}
                  open={showSubjectAlertPopover}
                />
              </Grid>
            </Grid>
          </Grid>
          <Grid item className={composerStyles.content}>
            {t('alertCustomTemplates.composer.content')}
            <Grid item className={clsx(commonStyles.editorBorder, composerStyles.editorArea)}>
              <Grid className={composerStyles.toolbarRoot} container alignItems="center">
                <Grid item className={composerStyles.formatIcons}>
                  {Object.keys(ToolbarMarkIcons).map((ic) =>
                    React.cloneElement(ToolbarMarkIcons[ic].icon, {
                      key: ic,
                      className: clsx(
                        ToolbarMarkIcons[ic].icon.props.className,
                        isMarkActive(bodyEditorRef.current, ic as TextDecorators) ? 'active' : ''
                      ),
                      onClick: (e: React.MouseEvent) => {
                        e.preventDefault();
                        toggleMark(bodyEditorRef.current!, ic as TextDecorators);
                        if (bodyEditorRef.current?.selection) {
                          ReactEditor.focus(bodyEditorRef.current);
                          Transforms.select(bodyEditorRef.current, bodyEditorRef.current.selection);
                        }
                      }
                    })
                  )}
                  {Object.keys(ToolbarBlockIcons).map((ic) =>
                    React.cloneElement(ToolbarBlockIcons[ic].icon, {
                      key: ic,
                      className: clsx(
                        ToolbarBlockIcons[ic].icon.props.className,
                        isBlockActive(bodyEditorRef.current, ToolbarBlockIcons[ic].align, 'align')
                          ? 'active'
                          : ''
                      ),
                      onClick: (e: React.MouseEvent) => {
                        e.preventDefault();
                        ToolbarBlockIcons[ic].fn(bodyEditorRef.current);
                      }
                    })
                  )}
                </Grid>
                <Grid item>
                  <GetInsertVariableButton
                    onClick={() => setShowBodyAlertPopover(true)}
                    ref={bodyInsertVariableButRef}
                  />
                  <AlertPopover
                    anchorEl={bodyInsertVariableButRef.current}
                    editor={bodyEditorRef.current as IYBEditor}
                    onVariableSelect={(variable, type) => {
                      if (type === 'SYSTEM') {
                        bodyEditorRef.current!['addSystemVariable'](variable);
                      } else {
                        bodyEditorRef.current!['addCustomVariable'](variable);
                      }
                    }}
                    handleClose={() => {
                      setShowBodyAlertPopover(false);
                    }}
                    open={showBodyAlertPopover}
                  />
                </Grid>
              </Grid>
              <YBEditor
                setVal={setBody}
                loadPlugins={{ alertVariablesPlugin: true }}
                ref={bodyEditorRef}
                // on onClick and on onKeyDown , update the counter state, re-render this component,
                // such that the marks (bold, italics) are marked
                editorProps={{
                  onClick: () => reRender()
                }}
                onEditorKeyDown={() => reRender()}
              />
            </Grid>
          </Grid>
          <Grid item container alignItems="center" className={composerStyles.helpText}>
            <Info />
            <Typography variant="body2">{t('alertCustomTemplates.composer.helpText')}</Typography>
          </Grid>
        </Grid>
        <Grid item className={commonStyles.noPadding}>
          <Grid
            container
            className={composerStyles.actions}
            alignItems="center"
            justifyContent="space-between"
          >
            <YBButton
              variant="secondary"
              onClick={() => {
                setShowPreviewModal(true);
              }}
            >
              {t('alertCustomTemplates.composer.previewTemplateButton')}
            </YBButton>
            <div>
              <YBButton
                variant="secondary"
                onClick={() => {
                  onClose();
                }}
              >
                {t('common.cancel')}
              </YBButton>
              <YBButton
                variant="primary"
                type="submit"
                disabled={
                  !isEditorDirty(subjectEditorRef.current) && !isEditorDirty(bodyEditorRef.current)
                }
                autoFocus
                className={composerStyles.submitButton}
                onClick={() => {
                  if (bodyEditorRef.current && subjectEditorRef.current) {
                    const subjectHtml = new TextSerializer(subjectEditorRef.current).serialize();
                    const bodyHtml = new HTMLSerializer(bodyEditorRef.current).serialize();

                    createTemplate.mutate({
                      textTemplate: bodyHtml,
                      titleTemplate: subjectHtml
                    });
                  }
                }}
              >
                {t('common.save')}
              </YBButton>
            </div>
          </Grid>
        </Grid>
        <EmailPreviewModal
          bodyValue={body}
          subjectValue={subject}
          visible={showPreviewModal}
          onHide={() => setShowPreviewModal(false)}
        />
      </>
    );
  }
);

export default EmailComposer;
